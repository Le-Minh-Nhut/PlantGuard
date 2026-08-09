import React, { type JSX, useState, useEffect, useRef } from "react";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Button } from "@/components/ui/button";
import { WiDaySunny } from "react-icons/wi";
import { MdOpacity, MdThermostat, MdWaterDrop } from "react-icons/md";
import { IoMdLeaf } from "react-icons/io";
import { Badge } from "@/components/ui/badge";

interface SensorData {
  temperature: number;
  humidity: number;
  soilMoisture: number;
}

interface Prediction {
  label: string;
  confidence: string;
  detection_box: [number, number, number, number];
}

interface DetectionResult {
  cropped_image_file: string;
  prediction: Prediction;
}

interface AiAnalysisResult {
  original_image: string;
  detections_found: number;
  results: DetectionResult[];
  json_file: string;
}

interface InfoBoxProps {
  icon: React.ReactNode;
  label: string;
  value: string;
}

export default function Dashboard(): JSX.Element {
  const [sensorData, setSensorData] = useState<SensorData | null>(null);
  const [selectedImage, setSelectedImage] = useState<File | null>(null);
  const [aiResult, setAiResult] = useState<AiAnalysisResult | null>(null);
  const [isLoading, setIsLoading] = useState<boolean>(false);
  const [error, setError] = useState<string | null>(null);
  const [liveFrameUrl, setLiveFrameUrl] =useState<string | null>(null);
  const liveFrameUrlRef = useRef<string | null>(null);

  useEffect(() => {
    const socket = new WebSocket("ws://localhost:8000/ws");
    socket.onmessage = (event) => {
      const msg = JSON.parse(event.data);
      setSensorData(msg);
    };
    return () => {
      socket.close();
    };
  }, []);

  useEffect(() => {
    const socket = new WebSocket("ws://192.168.1.28:8000/ws/camera/live");
    socket.onopen = () => {
      console.log("CAMERA VIEWER WS CONNECTED");
    };

    socket.onerror = (error) => {
      console.error("CAMERA VIEWER WS ERROR", error);
    };

    socket.onclose = () => {
      console.log("CAMERA VIEWER WS CLOSED");
    };
    socket.onmessage = (event) => {
      const blob = event.data as Blob;

      console.log(
        "CAMERA FRAME RECEIVED",
        blob.size,
        "bytes"
      );

      const newUrl = URL.createObjectURL(blob);

      if (liveFrameUrlRef.current) {
        URL.revokeObjectURL(liveFrameUrlRef.current);
      }

      liveFrameUrlRef.current = newUrl;
      setLiveFrameUrl(newUrl);
    };

    return () => {
      socket.close();
      if (liveFrameUrlRef.current) {
        URL.revokeObjectURL(liveFrameUrlRef.current);
      }
    };
  }, []);

  const handleImageUpload = async () => {
    if (!selectedImage) {
      alert("Vui lòng chọn một ảnh!");
      return;
    }

    setIsLoading(true);
    setError(null);
    setAiResult(null);

    try {
      const response = await fetch("http://localhost:8000/detect/image", {
        method: "POST",
        headers: {
          "Content-Type": "image/jpeg",
        },
        body: selectedImage,
      });

      if (!response.ok) {
        throw new Error(`Lỗi từ server: ${response.statusText}`);
      }

      const result: AiAnalysisResult = await response.json();
      setAiResult(result);
    } catch (err: any) {
      setError(`Không thể phân tích ảnh: ${err.message}`);
    } finally {
      setIsLoading(false);
    }
  };

  return (
    <div className="p-4 space-y-4">
      <div className="flex justify-between items-center">
        <div>
          <p className="text-sm text-gray-500">Ho Chi Minh, Vietnam</p>
          <p className="text-lg font-bold">Thứ Năm, 14 Tháng Tám 2025</p>
        </div>
        <div className="flex items-center space-x-2">
          <WiDaySunny className="text-3xl text-yellow-400" />
          <span className="text-2xl font-bold">28°C</span>
        </div>
      </div>

      <Card>
        <CardContent className="grid grid-cols-3 gap-4 p-4">
          <div className="col-span-3 flex items-center space-x-2">
            <IoMdLeaf className="text-green-500 text-2xl" />
            <span className="font-bold text-lg">PlantGuard</span>
            <span className="text-green-500">Dashboard</span>
          </div>
          <InfoBox icon={<MdThermostat />} label="Temp" value={sensorData ? `${sensorData.temperature.toFixed(1)}°C` : "N/A"} />
          <InfoBox icon={<MdOpacity />} label="Humidity" value={sensorData ? `${sensorData.humidity.toFixed(1)}%` : "N/A"} />
          <InfoBox icon={<MdWaterDrop />} label="Soil Moisture" value={sensorData ? `${sensorData.soilMoisture.toFixed(0)}%` : "N/A"} />
        </CardContent>
      </Card>
      <Card>
        <CardHeader>
          <CardTitle>Camera Trực Tiếp</CardTitle>
        </CardHeader>

        <CardContent className="p-4">
          <div className="w-full max-w-2xl mx-auto aspect-[4/3] bg-black rounded-lg overflow-hidden">
            {liveFrameUrl ? (
              <img
                src={liveFrameUrl}
                alt="PlantGuard Live Camera"
                className="w-full h-full object-contain"
              />
            ) : (
              <div className="w-full h-full flex items-center justify-center text-gray-400">
                Đang chờ ESP32-CAM...
              </div>
            )}
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Phân Tích Sức Khỏe Cây Trồng</CardTitle>
        </CardHeader>
        <CardContent className="p-4 space-y-4">
          <div className="flex items-center space-x-4">
            <input 
              type="file" 
              accept="image/jpeg, image/png"
              className="flex-grow"
              onChange={(e) => e.target.files && setSelectedImage(e.target.files[0])} 
            />
            <Button onClick={handleImageUpload} disabled={isLoading || !selectedImage}>
              {isLoading ? "Đang xử lý..." : "Phân tích"}
            </Button>
          </div>
          
          {isLoading && <p>Mô hình AI đang phân tích, vui lòng chờ...</p>}
          {error && <p className="text-red-500">{error}</p>}
          {aiResult && (
            <div className="mt-4 space-y-4">
              <h3 className="font-bold">Kết quả phân tích: <Badge>{`Phát hiện ${aiResult.detections_found} đối tượng`}</Badge></h3>
              {aiResult.results.length > 0 ? (
                aiResult.results.map((detection, index) => (
                  <div key={index} className="flex items-start space-x-4 border p-3 rounded-lg">
                    <img 
                      src={`http://localhost:8000/static/detection_results/${detection.cropped_image_file}`} 
                      alt={`Detection ${index + 1}`} 
                      className="w-32 h-32 object-cover rounded-md border"
                    />
                    <div className="text-left">
                      <p><strong>Bệnh:</strong> <Badge variant={detection.prediction.label.includes("healthy") ? "default" : "destructive"}>{detection.prediction.label}</Badge></p>
                      <p><strong>Độ tin cậy:</strong> {(parseFloat(detection.prediction.confidence) * 100).toFixed(2)}%</p>
                      <p className="text-xs text-gray-500 mt-2"><strong>Tọa độ Box:</strong> {`[${detection.prediction.detection_box.join(", ")}]`}</p>
                    </div>
                  </div>
                ))
              ) : (
                <p>Không phát hiện được lá cây nào trong ảnh.</p>
              )}
            </div>
          )}
        </CardContent>
      </Card>
    </div>
  );
}

function InfoBox({ icon, label, value }: InfoBoxProps): JSX.Element {
  return (
    <div className="bg-gray-100 dark:bg-gray-800 rounded-xl p-3 flex flex-col items-center justify-center">
      <div className="text-2xl mb-1">{icon}</div>
      <p className="text-xs text-gray-500 dark:text-gray-400">{label}</p>
      <p className="text-base font-semibold">{value}</p>
    </div>
  );
}
