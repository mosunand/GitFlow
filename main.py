import shutil
import sys
from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtGui import QIcon
from PySide6.QtWidgets import (
    QApplication,
    QDialog,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QVBoxLayout,
)

from gitflow.config.settings import Settings
from gitflow.services.account_service import AccountService
from gitflow.services.git_service import GitService, git_version, scan_all_git_paths
from gitflow.ui.main_window import MainWindow

DATA_DIR = Path(__file__).resolve().parent / "data"


def detect_git_path(settings: Settings) -> str | None:
    """Try to find a usable git executable. Returns path or None."""
    # 1. user-configured path
    saved = settings.git_path.strip()
    if saved:
        p = Path(saved)
        if p.exists() and p.is_file():
            return str(p)

    # 2. system PATH
    which = shutil.which("git")
    if which:
        return which

    # 3. common Windows install locations
    candidates = [
        r"C:\Program Files\Git\cmd\git.exe",
        r"C:\Program Files (x86)\Git\cmd\git.exe",
        r"D:\Git\cmd\git.exe",
    ]
    for c in candidates:
        if Path(c).exists():
            return c

    return None


class GitSetupDialog(QDialog):
    """Shown on first launch when git.exe cannot be auto-detected."""

    def __init__(self, settings: Settings, parent=None) -> None:
        super().__init__(parent)
        self.settings = settings
        self.setWindowTitle("GitFlow - 设置 Git 路径")
        self.setMinimumWidth(520)
        self.setMinimumHeight(180)
        self.setModal(True)

        layout = QVBoxLayout(self)

        desc = QLabel(
            "未检测到系统 Git 环境。\n"
            "请手动指定 git.exe 的完整路径，或点击「自动检测」重新扫描。"
        )
        desc.setWordWrap(True)
        layout.addWidget(desc)

        row = QHBoxLayout()
        self.path_edit = QLineEdit()
        self.path_edit.setPlaceholderText(r"C:\Program Files\Git\cmd\git.exe")
        self.path_edit.setText(settings.git_path)
        row.addWidget(self.path_edit, 1)

        browse_btn = QPushButton("浏览...")
        browse_btn.clicked.connect(self._browse)
        row.addWidget(browse_btn)
        layout.addLayout(row)

        btn_row = QHBoxLayout()
        detect_btn = QPushButton("自动检测")
        detect_btn.clicked.connect(self._auto_detect)
        btn_row.addWidget(detect_btn)

        btn_row.addStretch()

        cancel_btn = QPushButton("取消")
        cancel_btn.clicked.connect(self.reject)
        ok_btn = QPushButton("确认")
        ok_btn.clicked.connect(self._confirm)
        btn_row.addWidget(cancel_btn)
        btn_row.addWidget(ok_btn)
        layout.addLayout(btn_row)

        self.status_label = QLabel("")
        layout.addWidget(self.status_label)

    def _browse(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "选择 git.exe", "", "Git Executable (git.exe);;All Files (*)"
        )
        if path:
            self.path_edit.setText(path)

    def _auto_detect(self) -> None:
        results = scan_all_git_paths()
        if results:
            lines = [f"✅ 扫描到 {len(results)} 个 Git 安装："]
            for i, (path, ver) in enumerate(results):
                lines.append(f"  {i+1}. {path}")
                if ver:
                    lines.append(f"     {ver}")
            self.scan_results = results
            self.path_edit.setText(results[0][0])
            self.status_label.setText("\n".join(lines))
        else:
            self.scan_results = []
            self.path_edit.clear()
            self.status_label.setText("❌ 未在系统 PATH 和常见路径中检测到 Git。请手动浏览选择 git.exe。")

    def _confirm(self) -> None:
        path = self.path_edit.text().strip()
        if not path:
            QMessageBox.warning(self, "提示", "请输入 Git 路径。")
            return
        if not Path(path).exists():
            QMessageBox.warning(self, "路径无效", f"文件不存在：{path}")
            return
        ver = git_version(path)
        if not ver:
            QMessageBox.warning(self, "验证失败", "该路径不是有效的 git.exe。")
            return
        self.settings.git_path = path
        self.status_label.setText(f"✅ {ver}")
        self.accept()

    @property
    def selected_git_path(self) -> str:
        return self.path_edit.text().strip()


def _set_windows_taskbar_icon() -> None:
    """Force Windows taskbar to show our icon instead of the default python.exe icon."""
    try:
        import ctypes
        appid = "GitFlow.GitFlow.Desktop.1"
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(appid)
    except Exception:
        pass


def main() -> int:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    _set_windows_taskbar_icon()
    app = QApplication(sys.argv)
    app.setApplicationName("GitFlow")
    app.setWindowIcon(QIcon(str(Path(__file__).resolve().parent / "icon" / "logo.ico")))
    app.setStyle("Fusion")

    settings = Settings(DATA_DIR)

    # --- Git detection ---
    git_path = detect_git_path(settings)
    if not git_path:
        # no git found at all, show setup dialog
        dlg = GitSetupDialog(settings)
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return 0
        git_path = dlg.selected_git_path

    git_service = GitService(git_path=git_path)
    account_service = AccountService(DATA_DIR)

    window = MainWindow(git_service, account_service, settings)
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
