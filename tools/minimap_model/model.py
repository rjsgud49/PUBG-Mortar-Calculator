from pathlib import Path

import torch.nn as nn
import torchvision.models as models
from dataset import MinimapDataset


class MinimapModel(nn.Module):
    def __init__(self, num_classes=3, pretrained=True):
        super().__init__()
        weights = models.ResNet18_Weights.DEFAULT if pretrained else None
        self.model = models.resnet18(weights=weights)

        in_features = self.model.fc.in_features
        self.model.fc = nn.Linear(in_features, num_classes)

    def forward(self, x):
        return self.model(x)


if __name__ == "__main__":
    model = MinimapModel()

    dataset = MinimapDataset(Path(r"C:\Users\patri\Desktop\dataset"), False)

    image, label = dataset[0]

    image = image.unsqueeze(0)

    print(model(image))
