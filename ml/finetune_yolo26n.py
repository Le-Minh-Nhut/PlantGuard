from __future__ import annotations

import argparse
from pathlib import Path

from ultralytics import YOLO


def parse_args() -> argparse.Namespace:
    project_root = Path(__file__).resolve().parents[1]

    parser = argparse.ArgumentParser(
        description="Fine-tune YOLO26n for PlantGuard."
    )

    parser.add_argument(
        "--data",
        type=Path,
        default=project_root / "data" / "processed" / "plantguard_v1" / "dataset.yaml",
    )
    parser.add_argument("--epochs", type=int, default=100)
    parser.add_argument("--imgsz", type=int, default=320)
    parser.add_argument("--batch", type=int, default=-1)
    parser.add_argument("--device", default="0")
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--patience", type=int, default=20)

    return parser.parse_args()


def main() -> None:
    args = parse_args()

    project_root = Path(__file__).resolve().parents[1]
    runs_dir = project_root / "ml" / "runs"

    model = YOLO("yolo26n.pt")

    model.train(
        data=str(args.data),
        epochs=args.epochs,
        imgsz=args.imgsz,
        batch=args.batch,
        device=args.device,
        workers=args.workers,
        patience=args.patience,

        pretrained=True,
        amp=True,
        optimizer="auto",

        seed=42,

        project=str(runs_dir),
        name="plantguard_yolo26n",
        exist_ok=True,

        plots=True,
    )

    best_model_path = runs_dir/"plantguard_yolo26n"/"weights"/"best.pt"
    

    print("\nTraining finished.")
    print(f"Best model: {best_model_path}")

    best_model = YOLO(str(best_model_path))

    best_model.val(
        data=str(args.data),
        split="test",
        imgsz=args.imgsz,
        device=args.device,
        plots=True,
    )


if __name__ == "__main__":
    main()