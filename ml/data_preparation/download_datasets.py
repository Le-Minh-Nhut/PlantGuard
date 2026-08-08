from __future__ import annotations

import argparse
import os
from pathlib import Path

from roboflow import Roboflow


PLANTDOC_WORKSPACE = "joseph-nelson"
PLANTDOC_PROJECT = "plantdoc"
PLANTDOC_VERSION = 4


def download_dataset(
    rf: Roboflow,
    workspace: str,
    project: str,
    version: int,
    output_path: Path,
) -> None:
    print(f"\nDownloading {workspace}/{project} v{version} ...")

    dataset = (
        rf.workspace(workspace)
        .project(project)
        .version(version)
        .download(
            model_format="yolov8",
            location=str(output_path),
            overwrite=True,
        )
    )

    print(f"Saved to: {dataset.location}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download PlantGuard raw datasets from Roboflow."
    )

    parser.add_argument(
        "--atma-workspace",
        required=True,
        help="Workspace containing your clean Atma Jaya fork.",
    )
    parser.add_argument(
        "--atma-project",
        required=True,
        help="Project slug of your clean Atma Jaya fork.",
    )
    parser.add_argument(
        "--atma-version",
        type=int,
        required=True,
        help="Clean Atma Jaya dataset version with no offline augmentation.",
    )

    return parser.parse_args()


def main() -> None:
    args = parse_args()

    api_key = os.environ.get("ROBOFLOW_API_KEY")

    if not api_key:
        raise RuntimeError(
            "ROBOFLOW_API_KEY is not set.\n"
            "Run:\n"
            "export ROBOFLOW_API_KEY='your_api_key'"
        )

    project_root = Path(__file__).resolve().parents[2]

    raw_data_path = project_root / "data" / "raw"

    atma_output = raw_data_path / "atma_jaya"
    plantdoc_output = raw_data_path / "plantdoc_improved"

    raw_data_path.mkdir(parents=True, exist_ok=True)

    rf = Roboflow(api_key=api_key)

    download_dataset(
        rf=rf,
        workspace=args.atma_workspace,
        project=args.atma_project,
        version=args.atma_version,
        output_path=atma_output,
    )

    download_dataset(
        rf=rf,
        workspace=PLANTDOC_WORKSPACE,
        project=PLANTDOC_PROJECT,
        version=PLANTDOC_VERSION,
        output_path=plantdoc_output,
    )

    print("\nDone.")
    print(f"Atma Jaya: {atma_output}")
    print(f"PlantDoc:   {plantdoc_output}")


if __name__ == "__main__":
    main()
