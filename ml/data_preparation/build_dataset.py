from pathlib import Path
import random
import shutil

import yaml


TARGET_CLASSES = [
    "Bacterial_spot",
    "Early_blight",
    "Late_blight",
    "Leaf_Mold",
    "Septoria_leaf_spot",
    "Spider_mites",
    "Tomato_Yellow_Leaf_Curl_Virus",
    "Tomato_mosaic_virus",
]

TARGET_CLASS_IDS = {
    name: index
    for index, name in enumerate(TARGET_CLASSES)
}


ATMA_CLASS_MAP = {
    "Bacterial Spot": "Bacterial_spot",
    "Early Blight": "Early_blight",
    "Late Blight": "Late_blight",
    "Leaf Mold": "Leaf_Mold",
    "Septoria": "Septoria_leaf_spot",
    "Spider Mites": "Spider_mites",
    "Yellow Leaf Curl Virus": "Tomato_Yellow_Leaf_Curl_Virus",
    "Mosaic Virus": "Tomato_mosaic_virus",
}


PLANTDOC_CLASS_MAP = {
    "Tomato leaf bacterial spot": "Bacterial_spot",
    "Tomato Early blight leaf": "Early_blight",
    "Tomato leaf late blight": "Late_blight",
    "Tomato mold leaf": "Leaf_Mold",
    "Tomato Septoria leaf spot": "Septoria_leaf_spot",
    "Tomato two spotted spider mites leaf": "Spider_mites",
    "Tomato leaf yellow virus": "Tomato_Yellow_Leaf_Curl_Virus",
    "Tomato leaf mosaic virus": "Tomato_mosaic_virus",
}


def load_class_names(dataset_path: Path) -> dict[int, str]:
    yaml_path = dataset_path / "data.yaml"

    with open(yaml_path, "r", encoding="utf-8") as file:
        data = yaml.safe_load(file)

    names = data["names"]

    if isinstance(names, list):
        pairs = enumerate(names)
    else:
        pairs = names.items()

    class_names = {}

    for index, name in pairs:
        class_names[int(index)] = name

    return class_names


def load_samples(dataset_path: Path, class_map: dict[str, str], source_name: str) -> list[dict]:
    class_names = load_class_names(dataset_path)

    samples = []

    for split_name in ["train", "valid", "val", "test"]:
        image_dir = dataset_path / split_name / "images"
        label_dir = dataset_path / split_name / "labels"

        if not image_dir.exists():
            continue

        for image_path in image_dir.iterdir():
            if image_path.suffix.lower() not in [".jpg", ".jpeg", ".png"]:
                continue

            label_path = label_dir / f"{image_path.stem}.txt"

            if not label_path.exists():
                continue

            new_labels = []

            with open(label_path, "r", encoding="utf-8") as file:
                for line in file:
                    parts = line.strip().split()

                    if len(parts) != 5:
                        continue

                    old_class_id = int(float(parts[0]))
                    old_class_name = class_names[old_class_id]

                    if old_class_name not in class_map:
                        continue

                    new_class_name = class_map[old_class_name]
                    new_class_id = TARGET_CLASS_IDS[new_class_name]

                    x_center = parts[1]
                    y_center = parts[2]
                    width = parts[3]
                    height = parts[4]

                    new_line = (
                        f"{new_class_id} "
                        f"{x_center} "
                        f"{y_center} "
                        f"{width} "
                        f"{height}"
                    )

                    new_labels.append(new_line)

            if not new_labels:
                continue

            samples.append(
                {
                    "source": source_name,
                    "image_path": image_path,
                    "labels": new_labels,
                }
            )

    return samples


def split_samples(samples: list[dict], train_ratio: float = 0.8, val_ratio: float = 0.1):
    random.seed(42)
    random.shuffle(samples)

    total = len(samples)

    train_end = int(total * train_ratio)
    val_end = train_end + int(total * val_ratio)

    train_samples = samples[:train_end]
    val_samples = samples[train_end:val_end]
    test_samples = samples[val_end:]

    return {
        "train": train_samples,
        "val": val_samples,
        "test": test_samples,
    }


def save_split(samples: list[dict], split_name: str, output_path: Path):
    image_output = output_path / "images" / split_name
    label_output = output_path / "labels" / split_name

    image_output.mkdir(parents=True, exist_ok=True)
    label_output.mkdir(parents=True, exist_ok=True)

    for index, sample in enumerate(samples):
        source = sample["source"]
        image_path = sample["image_path"]

        new_name = f"{source}_{index:06d}{image_path.suffix.lower()}"

        destination_image = image_output / new_name

        shutil.copy2(
            image_path,
            destination_image,
        )

        destination_label = (
            label_output
            / f"{Path(new_name).stem}.txt"
        )

        with open(
            destination_label,
            "w",
            encoding="utf-8",
        ) as file:
            file.write("\n".join(sample["labels"]))


def save_dataset_yaml(output_path: Path):
    data = {
        "train": "images/train",
        "val": "images/val",
        "test": "images/test",
        "names": TARGET_CLASSES,
    }

    yaml_path = output_path / "dataset.yaml"

    with open(
        yaml_path,
        "w",
        encoding="utf-8",
    ) as file:
        yaml.safe_dump(
            data,
            file,
            sort_keys=False,
        )


def main():
    project_root = Path(__file__).resolve().parents[2]

    atma_path = project_root/"data"/"raw"/"atma_jaya"

    plantdoc_path = project_root/"data"/"raw"/"plantdoc_improved"

    output_path = project_root/"data"/"processed"/"plantguard_v1"

    atma_samples = load_samples(
        dataset_path=atma_path,
        class_map=ATMA_CLASS_MAP,
        source_name="atma",
    )

    plantdoc_samples = load_samples(
        dataset_path=plantdoc_path,
        class_map=PLANTDOC_CLASS_MAP,
        source_name="plantdoc",
    )

    samples = atma_samples + plantdoc_samples

    print(f"Atma Jaya: {len(atma_samples)} images")
    print(f"PlantDoc: {len(plantdoc_samples)} images")
    print(f"Total: {len(samples)} images")

    splits = split_samples(samples)

    for split_name, split_data in splits.items():
        save_split(
            samples=split_data,
            split_name=split_name,
            output_path=output_path,
        )

        print(
            f"{split_name}: "
            f"{len(split_data)} images"
        )

    save_dataset_yaml(output_path)

    print()
    print("Dataset created at:")
    print(output_path)


if __name__ == "__main__":
    main()