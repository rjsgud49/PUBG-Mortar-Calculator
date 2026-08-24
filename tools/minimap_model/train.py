from pathlib import Path

import torch
import torchvision
import torchvision.models as models
from dataset import MinimapDataset, SquarePad
from matplotlib import pyplot
from model import MinimapModel
from torch import nn
from torch.utils.data import DataLoader
from torchvision import transforms

model = MinimapModel().to("cuda")
optimizer = torch.optim.Adam(model.parameters(), lr=0.0003)
loss_function = nn.CrossEntropyLoss().to("cuda")

transform = torchvision.transforms.Compose(
    [
        SquarePad(),
        transforms.Resize((224, 224)),
        transforms.ColorJitter(brightness=0.1, contrast=0.1),
        transforms.RandomHorizontalFlip(p=0.5),
        transforms.RandomVerticalFlip(p=0.5),
        transforms.ToTensor(),
        transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225]),
    ]
)

train_dataset = MinimapDataset(
    Path(r"C:\Users\patri\Desktop\dataset"), transform=transform
)
train_dataloader = DataLoader(train_dataset, 8, shuffle=True)

transform = torchvision.transforms.Compose(
    [
        SquarePad(),
        transforms.Resize((224, 224)),
        transforms.ToTensor(),
        transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225]),
    ]
)


val_dataset = MinimapDataset(
    Path(r"C:\Users\patri\Desktop\dataset"), False, transform=transform
)
val_dataloader = DataLoader(val_dataset, 8)
best_val_loss = float("inf")

for epoch in range(21):
    model.train()
    total_train_loss = 0.0

    for images, labels in train_dataloader:
        images = images.to("cuda").float()
        labels = labels.to("cuda")

        optimizer.zero_grad()
        predictions = model(images)
        loss = loss_function(predictions, labels)

        loss.backward()
        optimizer.step()

        total_train_loss += loss.item()

    avg_train_loss = total_train_loss / len(train_dataloader)

    if epoch % 5 == 0:
        model.eval()
        total_val_loss = 0.0
        correct = 0
        total = 0

        with torch.inference_mode():
            for val_images, val_labels in val_dataloader:
                val_images = val_images.to("cuda").float()
                val_labels = val_labels.to("cuda")

                val_preds = model(val_images)
                val_loss = loss_function(val_preds, val_labels)
                total_val_loss += val_loss.item()

                predicted_classes = torch.argmax(val_preds, dim=1)
                correct += (predicted_classes == val_labels).sum().item()
                total += val_labels.size(0)

        avg_val_loss = total_val_loss / len(val_dataloader)
        accuracy = (correct / total) * 100

        if avg_val_loss < best_val_loss:
            best_val_loss = avg_val_loss
            torch.save(model.state_dict(), Path("tools\\minimap_model\\best.pth"))

        print(
            f"Epoch: {epoch:03d} | Train Loss: {avg_train_loss:.4f} | Val Loss: {avg_val_loss:.4f} | Val Acc: {accuracy:.2f}%"
        )

model.eval()
print("\n--- Final Predictions Sample ---")
with torch.inference_mode():
    for val_images, val_labels in val_dataloader:
        val_images = val_images.to("cuda").float()
        outputs = model(val_images)
        predicted_classes = torch.argmax(outputs, dim=1)

        for pred, actual in zip(predicted_classes[:10], val_labels[:10]):
            print(f"Predicted: {pred.item()} | Actual: {actual.item()}")
        break
