#pragma once

static const char *plantguard_classes[] = {
    "Bacterial_spot",
    "Early_blight",
    "Late_blight",
    "Leaf_Mold",
    "Septoria_leaf_spot",
    "Spider_mites",
    "Tomato_Yellow_Leaf_Curl_Virus",
    "Tomato_mosaic_virus",
};

static constexpr int PLANTGUARD_NUM_CLASSES =
    sizeof(plantguard_classes) /
    sizeof(plantguard_classes[0]);