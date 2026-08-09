import { useEffect, useRef, useState, type JSX, type ReactNode } from "react";
import {
  Activity,
  Camera,
  Droplets,
  Leaf,
  Radio,
  Sprout,
  Thermometer,
  Wifi,
  WifiOff,
} from "lucide-react";

interface SensorData {
  temperature: number;
  humidity: number;
  soilMoisture: number;
}

interface EdgeDetection {
  class_id: number;
  class: string;
  confidence: number;
  bbox: [number, number, number, number];
}

interface EdgeLatency {
  decode_ms: number;
  preprocess_ms: number;
  inference_ms: number;
  postprocess_ms: number;
  total_ms: number;
}

interface EdgeInferenceResult {
  device_id: string;
  frame_id: number;
  timestamp_us?: number;
  detection_count: number;
  detections: EdgeDetection[];
  latency: EdgeLatency;
}

type SocketStatus = "connecting" | "connected" | "disconnected";
const backendHost = import.meta.env.VITE_BACKEND_HOST ?? window.location.hostname;
const SENSOR_WS_URL = import.meta.env.VITE_SENSOR_WS_URL ?? `ws://${backendHost}:8000/ws`;
const CAMERA_WS_URL = import.meta.env.VITE_CAMERA_WS_URL ?? `ws://${backendHost}:8000/ws/camera/live`;
const DETECTION_WS_URL = import.meta.env.VITE_DETECTION_WS_URL ?? `ws://${backendHost}:8000/ws/detections`;

function useSensorSocket(url: string) {
  const [sensorData, setSensorData] = useState<SensorData | null>(null);
  const [status, setStatus] = useState<SocketStatus>("connecting");

  useEffect(() => {
    let socket: WebSocket | null = null;
    let reconnectTimer: number | null = null;
    let disposed = false;

    const connect = () => {
      if (disposed) return;

      setStatus("connecting");
      socket = new WebSocket(url);

      socket.onopen = () => {
        setStatus("connected");
      };

      socket.onmessage = (event) => {
        if (typeof event.data !== "string") return;

        try {
          const data = JSON.parse(event.data) as SensorData;
          setSensorData(data);
        } catch (error) {
          console.error("Invalid sensor payload:", error);
        }
      };

      socket.onerror = () => {
        socket?.close();
      };

      socket.onclose = () => {
        if (disposed) return;

        setStatus("disconnected");
        reconnectTimer = window.setTimeout(connect, 1500);
      };
    };

    connect();

    return () => {
      disposed = true;

      if (reconnectTimer !== null) {
        window.clearTimeout(reconnectTimer);
      }

      socket?.close();
    };
  }, [url]);

  return { sensorData, status };
}

function useCameraStream(url: string) {
  const [frameUrl, setFrameUrl] = useState<string | null>(null);
  const [status, setStatus] = useState<SocketStatus>("connecting");
  const [lastFrameAt, setLastFrameAt] = useState<Date | null>(null);

  const currentFrameUrl = useRef<string | null>(null);

  useEffect(() => {
    let socket: WebSocket | null = null;
    let reconnectTimer: number | null = null;
    let disposed = false;

    const connect = () => {
      if (disposed) return;

      setStatus("connecting");

      socket = new WebSocket(url);
      socket.binaryType = "blob";

      socket.onopen = () => {
        setStatus("connected");
      };

      socket.onmessage = (event) => {
        const blob =
          event.data instanceof Blob
            ? event.data
            : new Blob([event.data], { type: "image/jpeg" });

        const nextUrl = URL.createObjectURL(blob);
        const previousUrl = currentFrameUrl.current;

        currentFrameUrl.current = nextUrl;
        setFrameUrl(nextUrl);
        setLastFrameAt(new Date());

        if (previousUrl) {
          URL.revokeObjectURL(previousUrl);
        }
      };

      socket.onerror = () => {
        socket?.close();
      };

      socket.onclose = () => {
        if (disposed) return;

        setStatus("disconnected");
        reconnectTimer = window.setTimeout(connect, 1500);
      };
    };

    connect();

    return () => {
      disposed = true;

      if (reconnectTimer !== null) {
        window.clearTimeout(reconnectTimer);
      }

      socket?.close();

      if (currentFrameUrl.current) {
        URL.revokeObjectURL(currentFrameUrl.current);
        currentFrameUrl.current = null;
      }
    };
  }, [url]);

  return { frameUrl, status, lastFrameAt };
}

function useDetectionSocket(url: string) {
  const [result, setResult] =
    useState<EdgeInferenceResult | null>(null);

  const [status, setStatus] =
    useState<SocketStatus>("connecting");

  useEffect(() => {
    let socket: WebSocket | null = null;
    let reconnectTimer: number | null = null;
    let disposed = false;

    const connect = () => {
      if (disposed) return;

      setStatus("connecting");

      socket = new WebSocket(url);

      socket.onopen = () => {
        console.log("DETECTION WS CONNECTED");
        setStatus("connected");
      };

      socket.onmessage = (event) => {
        try {
          const payload =
            JSON.parse(event.data) as EdgeInferenceResult;

          console.log(
            "EDGE AI RESULT",
            payload,
          );

          setResult(payload);
        } catch (error) {
          console.error(
            "Invalid detection payload:",
            error,
          );
        }
      };

      socket.onerror = () => {
        socket?.close();
      };

      socket.onclose = () => {
        if (disposed) return;

        setStatus("disconnected");

        reconnectTimer =
          window.setTimeout(
            connect,
            1500,
          );
      };
    };

    connect();

    return () => {
      disposed = true;

      if (reconnectTimer !== null) {
        window.clearTimeout(
          reconnectTimer,
        );
      }

      socket?.close();
    };
  }, [url]);

  return {
    result,
    status,
  };
}

export default function Dashboard(): JSX.Element {
  const { sensorData, status: sensorStatus } = useSensorSocket(SENSOR_WS_URL);
  const {frameUrl, status: cameraStatus, lastFrameAt} = useCameraStream(CAMERA_WS_URL);

  const {result: edgeResult, status: detectionStatus} = useDetectionSocket(DETECTION_WS_URL);

  const dateText = new Intl.DateTimeFormat("vi-VN", {
    weekday: "long",
    day: "2-digit",
    month: "long",
    year: "numeric",
  }).format(new Date());

  return (
    <main className="min-h-screen bg-[#f4f6f2] text-[#172018]">
      <header className="border-b border-[#dfe5dc] bg-[#fbfcfa]">
        <div className="mx-auto flex max-w-7xl items-center justify-between px-5 py-4 lg:px-8">
          <div className="flex items-center gap-3">
            <div className="grid size-10 place-items-center rounded-xl bg-[#173f2a] text-white">
              <Leaf size={21} strokeWidth={2.2} />
            </div>

            <div className="text-left">
              <p className="text-base font-semibold tracking-[-0.02em]">
                PlantGuard
              </p>
              <p className="text-xs text-[#738077]">
                Edge plant monitoring system
              </p>
            </div>
          </div>

          <ConnectionPill
            connected={ cameraStatus === "connected" && sensorStatus === "connected" && detectionStatus === "connected"}
          />
        </div>
      </header>

      <div className="mx-auto max-w-7xl px-5 py-7 lg:px-8 lg:py-10">
        <section className="mb-7 flex flex-col gap-3 sm:flex-row sm:items-end sm:justify-between">
          <div className="text-left">
            <p className="mb-2 text-xs font-semibold uppercase tracking-[0.16em] text-[#657269]">
              Garden overview
            </p>
            <h1 className="text-3xl font-semibold tracking-[-0.04em] sm:text-4xl">
              Theo dõi khu vườn
            </h1>
          </div>

          <p className="text-sm capitalize text-[#718078]">{dateText}</p>
        </section>

        <section className="mb-6 grid gap-3 sm:grid-cols-3">
          <MetricCard
            icon={<Thermometer size={19} />}
            label="Nhiệt độ"
            value={
              sensorData ? `${sensorData.temperature.toFixed(1)}°C` : "—"
            }
          />

          <MetricCard
            icon={<Droplets size={19} />}
            label="Độ ẩm không khí"
            value={sensorData ? `${sensorData.humidity.toFixed(1)}%` : "—"}
          />

          <MetricCard
            icon={<Sprout size={19} />}
            label="Độ ẩm đất"
            value={
              sensorData ? `${sensorData.soilMoisture.toFixed(0)}%` : "—"
            }
          />
        </section>

        <section className="grid gap-5 lg:grid-cols-[minmax(0,1fr)_290px]">
          <div className="overflow-hidden rounded-2xl border border-[#dce3da] bg-white shadow-[0_1px_2px_rgba(20,40,24,0.04)]">
            <div className="flex items-center justify-between border-b border-[#e4e9e1] px-5 py-4">
              <div className="flex items-center gap-3">
                <Camera size={19} className="text-[#405047]" />

                <div className="text-left">
                  <h2 className="text-sm font-semibold">Camera khu vườn</h2>
                  <p className="text-xs text-[#7a877f]">
                    ESP32-CAM · realtime JPEG stream
                  </p>
                </div>
              </div>

              <StreamBadge status={cameraStatus} />
            </div>

            <div className="relative aspect-video min-h-[300px] bg-[#111712] sm:min-h-[420px]">
              {frameUrl ? (
                <img
                  src={frameUrl}
                  alt="PlantGuard realtime camera"
                  className="h-full w-full object-contain"
                />
              ) : (
                <div className="absolute inset-0 grid place-items-center">
                  <div className="text-center text-white/70">
                    <Camera className="mx-auto mb-3" size={30} />
                    <p className="text-sm font-medium">
                      {cameraStatus === "connecting"
                        ? "Đang kết nối camera..."
                        : "Chưa nhận được frame"}
                    </p>
                    <p className="mt-1 text-xs text-white/40">
                      {CAMERA_WS_URL}
                    </p>
                  </div>
                </div>
              )}

              {frameUrl && (
                <div className="pointer-events-none absolute left-4 top-4 flex items-center gap-2 rounded-md bg-black/65 px-2.5 py-1.5 text-[11px] font-semibold uppercase tracking-[0.12em] text-white backdrop-blur-sm">
                  <span className="size-2 rounded-full bg-[#65d082]" />
                  Live
                </div>
              )}
            </div>
          </div>

          <aside className="space-y-4">
            <div className="rounded-2xl border border-[#dce3da] bg-white p-5">
              <div className="mb-5 flex items-center gap-2 text-left">
                <Activity size={18} className="text-[#405047]" />
                <h2 className="text-sm font-semibold">Trạng thái hệ thống</h2>
              </div>

              <div className="space-y-4">
                <StatusRow label="Camera stream" status={cameraStatus} icon={<Camera size={16} />}/>
                <StatusRow label="Sensor stream" status={sensorStatus} icon={<Radio size={16} />} />
                <StatusRow label="Edge AI" status={detectionStatus} icon={<Activity size={16} />}/>
              </div>
            </div>
            <div className="rounded-2xl border border-[#dce3da] bg-white p-5 text-left">
              <div className="mb-4 flex items-center justify-between">
                <div>
                  <p className="text-sm font-semibold">
                    Edge AI
                  </p>

                  <p className="mt-1 text-xs text-[#7a877f]">
                    ESP32-S3 · YOLO26n
                  </p>
                </div>

                <StreamBadge status={detectionStatus} />
              </div>

              {!edgeResult ? (
                <p className="text-sm text-[#758079]">
                  Đang chờ kết quả từ ESP32-S3...
                </p>
              ) : (
                <div className="space-y-4">
                  <div className="flex items-end justify-between">
                    <div>
                      <p className="text-xs text-[#7a877f]">
                        AI frame
                      </p>

                      <p className="mt-1 text-lg font-semibold">
                        #{edgeResult.frame_id}
                      </p>
                    </div>

                    <div className="text-right">
                      <p className="text-xs text-[#7a877f]">
                        Inference
                      </p>

                      <p className="mt-1 text-sm font-semibold">
                        {(edgeResult.latency.inference_ms / 1000).toFixed(2)} s
                      </p>
                    </div>
                  </div>

                  <div className="h-px bg-[#e7ebe5]" />

                  {edgeResult.detections.length === 0 ? (
                    <p className="text-sm text-[#758079]">
                      Không phát hiện bệnh trong frame gần nhất.
                    </p>
                  ) : (
                    <div className="space-y-2">
                      {edgeResult.detections.map((detection, index) => (
                        <div
                          key={`${edgeResult.frame_id}-${index}`}
                          className="rounded-xl bg-[#f4f6f2] p-3"
                        >
                          <div className="flex items-center justify-between gap-3">
                            <span className="text-sm font-medium">
                              {detection.class.replaceAll("_", " ")}
                            </span>

                            <span className="text-xs font-semibold text-[#446a50]">
                              {(detection.confidence * 100).toFixed(1)}%
                            </span>
                          </div>
                        </div>
                      ))}
                    </div>
                  )}
                </div>
              )}
            </div>

            <div className="rounded-2xl border border-[#dce3da] bg-white p-5 text-left">
              <p className="mb-1 text-xs font-medium text-[#7a877f]">
                Frame gần nhất
              </p>

              <p className="text-sm font-semibold">
                {lastFrameAt
                  ? lastFrameAt.toLocaleTimeString("vi-VN")
                  : "Chưa nhận dữ liệu"}
              </p>

              <div className="my-4 h-px bg-[#e7ebe5]" />

              <p className="text-xs leading-5 text-[#758079]">
                Camera được truyền trực tiếp từ server qua WebSocket. Không còn
                upload ảnh thủ công trên dashboard.
              </p>
            </div>
          </aside>
        </section>
      </div>
    </main>
  );
}

interface MetricCardProps {
  icon: ReactNode;
  label: string;
  value: string;
}

function MetricCard({ icon, label, value }: MetricCardProps): JSX.Element {
  return (
    <article className="rounded-2xl border border-[#dce3da] bg-white p-5 text-left shadow-[0_1px_2px_rgba(20,40,24,0.03)]">
      <div className="mb-5 flex size-9 items-center justify-center rounded-lg bg-[#edf2ec] text-[#31533d]">
        {icon}
      </div>

      <p className="text-xs font-medium text-[#768179]">{label}</p>
      <p className="mt-1 text-2xl font-semibold tracking-[-0.03em]">{value}</p>
    </article>
  );
}

function ConnectionPill({ connected }: { connected: boolean }): JSX.Element {
  return (
    <div
      className={`flex items-center gap-2 rounded-full border px-3 py-1.5 text-xs font-medium ${
        connected
          ? "border-[#cfe2d2] bg-[#f2f8f2] text-[#2d6640]"
          : "border-[#e7ddd0] bg-[#faf6f0] text-[#765a36]"
      }`}
    >
      {connected ? <Wifi size={14} /> : <WifiOff size={14} />}
      {connected ? "System online" : "Connecting"}
    </div>
  );
}

function StreamBadge({ status }: { status: SocketStatus }): JSX.Element {
  const label =
    status === "connected"
      ? "Live"
      : status === "connecting"
        ? "Connecting"
        : "Offline";

  return (
    <div className="flex items-center gap-2 text-xs font-medium text-[#647168]">
      <span
        className={`size-2 rounded-full ${
          status === "connected"
            ? "bg-[#46a864]"
            : status === "connecting"
              ? "bg-[#d39b43]"
              : "bg-[#b86464]"
        }`}
      />
      {label}
    </div>
  );
}

interface StatusRowProps {
  label: string;
  status: SocketStatus;
  icon: ReactNode;
}

function StatusRow({ label, status, icon }: StatusRowProps): JSX.Element {
  const statusText =
    status === "connected"
      ? "Connected"
      : status === "connecting"
        ? "Connecting"
        : "Disconnected";

  return (
    <div className="flex items-center justify-between gap-3">
      <div className="flex items-center gap-2.5 text-sm text-[#4f5c54]">
        {icon}
        <span>{label}</span>
      </div>

      <span
        className={`text-xs font-medium ${
          status === "connected"
            ? "text-[#3e8052]"
            : status === "connecting"
              ? "text-[#956f34]"
              : "text-[#9b5050]"
        }`}
      >
        {statusText}
      </span>
    </div>
  );
}
