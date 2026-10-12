"""One-second device resource table and rolling trend plot."""
from collections import deque

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor, QPainter, QPen
from PySide6.QtWidgets import QAbstractItemView, QTableWidget, QTableWidgetItem, QWidget


class ResourceChart(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.cpu = deque(maxlen=60)
        self.ram = deque(maxlen=60)
        self.setMinimumHeight(170)

    def add_sample(self, cpu, used_bytes):
        self.cpu.append(None if cpu == 255 else int(cpu))
        self.ram.append(min(100, round(100 * used_bytes / 32768)))
        self.update()

    def paintEvent(self, _event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        rect = self.rect().adjusted(40, 14, -12, -25)
        painter.fillRect(self.rect(), QColor("#ffffff"))
        painter.setPen(QPen(QColor("#d8e3ef"), 1))
        for level in (0, 25, 50, 75, 100):
            y = rect.bottom() - rect.height() * level / 100
            painter.drawLine(rect.left(), int(y), rect.right(), int(y))
            painter.drawText(3, int(y) + 4, f"{level}%")
        painter.setPen(QColor("#6a7890"))
        painter.drawText(rect.left(), self.height() - 5, "过去 60 秒 ←  每秒一点")
        painter.setPen(QColor("#1769c2"))
        painter.drawText(rect.right() - 138, self.height() - 5, "● CPU 主循环")
        painter.setPen(QColor("#16966c"))
        painter.drawText(rect.right() - 45, self.height() - 5, "● RAM")
        for samples, color in ((self.cpu, "#1769c2"), (self.ram, "#16966c")):
            painter.setPen(QPen(QColor(color), 2))
            previous = None
            for i, sample in enumerate(samples):
                if sample is None: previous = None; continue
                x = rect.right() - (len(samples) - 1 - i) * rect.width() / 59
                y = rect.bottom() - rect.height() * sample / 100
                if previous: painter.drawLine(int(previous[0]), int(previous[1]), int(x), int(y))
                painter.drawEllipse(int(x) - 2, int(y) - 2, 4, 4)
                previous = (x, y)


class ResourceTable(QTableWidget):
    LABELS = ("CPU 主循环忙碌率", "RAM 静态占用", "RAM 按需分配", "RAM 可分配", "栈预留", "上位机镜像容量")

    def __init__(self, parent=None):
        super().__init__(len(self.LABELS), 2, parent)
        self.setHorizontalHeaderLabels(["资源", "当前值"])
        self.verticalHeader().setVisible(False)
        self.setSelectionMode(QAbstractItemView.SelectionMode.NoSelection)
        self.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.setMinimumHeight(220)
        self.setColumnWidth(0, 200)
        self.horizontalHeader().setStretchLastSection(True)
        for row, label in enumerate(self.LABELS):
            self.setItem(row, 0, QTableWidgetItem(label))
            self.setItem(row, 1, QTableWidgetItem("—"))

    def update_metrics(self, metrics, host_image_bytes):
        values = ("采样中" if metrics.cpu_percent == 255 else f"{metrics.cpu_percent}%",
                  f"{metrics.static_bytes} B", f"{metrics.allocated_bytes} B",
                  f"{metrics.available_bytes} B", f"{metrics.stack_reserve_bytes} B",
                  f"{host_image_bytes} B" if host_image_bytes else "未启用")
        for row, value in enumerate(values): self.item(row, 1).setText(value)
