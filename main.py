import sys
import os
import time
import posixpath # Chuyên gia xử lý đường dẫn chuẩn Linux/Thẻ nhớ an toàn
from PyQt5.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QGroupBox,
                             QPushButton, QLineEdit, QTableWidget, QTableWidgetItem, 
                             QHeaderView, QProgressBar, QLabel, QFileDialog, QMessageBox, 
                             QMenu, QToolBar, QStatusBar, QStyle, QShortcut, QInputDialog,
                             QDialog, QTextEdit)
from PyQt5.QtCore import Qt, QThread, pyqtSignal
from PyQt5.QtGui import QKeySequence
from protocol import SDCardProtocol

class LiveStatusDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Live Status - Đối chiếu PC & STM32")
        self.resize(750, 450)
        
        layout = QVBoxLayout()
        compare_layout = QHBoxLayout()
        
        # Cột PC
        pc_group = QGroupBox("PC (App) Ghi nhận")
        pc_layout = QVBoxLayout()
        self.pc_lbl_op = QLabel("Trạng thái: -")
        self.pc_lbl_speed = QLabel("Tốc độ: 0.0 KB/s")
        self.pc_lbl_speed.setStyleSheet("font-weight: bold; color: blue;")
        self.pc_progress = QProgressBar()
        self.pc_progress.setValue(0)
        pc_layout.addWidget(self.pc_lbl_op)
        pc_layout.addWidget(self.pc_lbl_speed)
        pc_layout.addWidget(self.pc_progress)
        pc_group.setLayout(pc_layout)
        
        # Cột STM32
        stm_group = QGroupBox("STM32 Ghi nhận")
        stm_layout = QVBoxLayout()
        self.stm_lbl_op = QLabel("Trạng thái: -")
        self.stm_lbl_speed = QLabel("Tốc độ: 0.0 KB/s")
        self.stm_lbl_speed.setStyleSheet("font-weight: bold; color: green;")
        self.stm_progress = QProgressBar()
        self.stm_progress.setValue(0)
        stm_layout.addWidget(self.stm_lbl_op)
        stm_layout.addWidget(self.stm_lbl_speed)
        stm_layout.addWidget(self.stm_progress)
        stm_group.setLayout(stm_layout)
        
        compare_layout.addWidget(pc_group)
        compare_layout.addWidget(stm_group)
        layout.addLayout(compare_layout)
        
        self.log_output = QTextEdit()
        self.log_output.setReadOnly(True)
        self.log_output.setStyleSheet("background-color: #1e1e1e; color: #00ff00; font-family: Consolas; font-size: 13px;")
        layout.addWidget(self.log_output)
        
        self.btn_close = QPushButton("Đóng")
        self.btn_close.setEnabled(False)
        self.btn_close.clicked.connect(self.accept)
        layout.addWidget(self.btn_close)
        
        self.setLayout(layout)
        
        self.last_stm_op = -1
        self.last_stm_error = 0
        
    def log(self, text):
        t = time.strftime("[%H:%M:%S]")
        self.log_output.append(f"{t} {text}")
        
    def update_pc_status(self, percent, speed_kbps, op_name, filename):
        self.pc_progress.setRange(0, 100)
        self.pc_lbl_op.setText(f"[{op_name}] {filename}")
        self.pc_lbl_speed.setText(f"Tốc độ: {speed_kbps:.1f} KB/s")
        self.pc_progress.setValue(percent)

    def update_pc_text(self, text):
        self.pc_lbl_op.setText(text)
        self.pc_progress.setRange(0, 0)
        self.pc_lbl_speed.setText("")
        
    def update_stm_status(self, status):
        if not status: return
        op_map = {0: "IDLE", 1: "UPLOAD", 2: "DOWNLOAD", 3: "DELETE", 4: "MKDIR", 5: "MOVE"}
        op_str = op_map.get(status['op_type'], "UNKNOWN")
        
        self.stm_progress.setRange(0, 100)
        self.stm_progress.setValue(status['progress_percent'])
        
        if status['last_error'] != 0:
            self.stm_lbl_op.setText(f"LỖI FATFS: {status['last_error']}")
            if status['last_error'] != self.last_stm_error:
                self.log(f"STM32: LỖI FatFs Code = {status['last_error']} khi xử lý '{status['filename']}'")
                self.last_stm_error = status['last_error']
        else:
            self.stm_lbl_op.setText(f"[{op_str}] {status['filename']}")
            self.stm_lbl_speed.setText(f"Tốc độ: {status['speed_kbps']:.1f} KB/s")
            
        if status['op_type'] != self.last_stm_op:
            if status['op_type'] != 0:
                self.log(f"STM32: Bắt đầu xử lý {op_str} cho file '{status['filename']}'")
            self.last_stm_op = status['op_type']

    def finish(self, msg):
        self.log(f"--- HOÀN TẤT: {msg} ---")
        self.pc_lbl_op.setText(f"Xong: {msg}")
        self.pc_progress.setRange(0, 100)
        self.pc_progress.setValue(100)
        self.pc_lbl_speed.setText("")
        
        self.stm_lbl_op.setText("Hoàn tất")
        self.stm_progress.setRange(0, 100)
        self.stm_progress.setValue(100)
        self.stm_lbl_speed.setText("")
        
        self.btn_close.setEnabled(True)


class TransferWorker(QThread):
    progress = pyqtSignal(int, int)
    finished = pyqtSignal(bool, str)
    live_status = pyqtSignal(dict)

    def __init__(self, protocol, task_type, local_path, remote_path):
        super().__init__()
        self.protocol = protocol 
        self.task_type = task_type
        self.local_path = local_path
        self.remote_path = remote_path
        self.last_status_time = 0

    def do_progress(self, current, total):
        if time.time() - self.last_status_time > 0.5:
            self.last_status_time = time.time()
            st = self.protocol.get_ui_live_status()
            if st:
                self.live_status.emit(st)
        self.progress.emit(current, total)

    def run(self):
        try:
            if self.task_type == "DOWNLOAD":
                res = self.protocol.download_file(self.remote_path, self.local_path, self.do_progress)
                self.finished.emit(res, "Tải xuống thành công!" if res else "Lỗi tải xuống!")
            elif self.task_type == "UPLOAD":
                res = self.protocol.upload_file(self.local_path, self.remote_path, self.do_progress)
                self.finished.emit(res, "Tải lên thành công!" if res else "Lỗi tải lên!")
            elif self.task_type in ["COPY_SD_TO_SD", "CUT_SD_TO_SD"]:
                import tempfile
                source_remote_path = self.local_path 
                temp_file_path = os.path.join(tempfile.gettempdir(), "sd_temp_copy.dat")
                
                def download_progress(current, total):
                    if total > 0: self.do_progress(current, total * 2)
                        
                def upload_progress(current, total):
                    if total > 0: self.do_progress(total + current, total * 2)

                res_down = self.protocol.download_file(source_remote_path, temp_file_path, download_progress)
                if not res_down:
                    err = getattr(self.protocol, 'last_error', 'Unknown')
                    self.finished.emit(False, f"Lỗi tải file gốc để copy/cut! ({err})")
                    if os.path.exists(temp_file_path): os.remove(temp_file_path)
                    return
                    
                res_up = self.protocol.upload_file(temp_file_path, self.remote_path, upload_progress)
                if os.path.exists(temp_file_path): os.remove(temp_file_path)
                
                if not res_up:
                    err = getattr(self.protocol, 'last_error', 'Unknown')
                    self.finished.emit(False, f"Lỗi ghi file đích! ({err})")
                    return
                    
                if self.task_type == "CUT_SD_TO_SD":
                    if not self.protocol.delete_item(source_remote_path):
                        self.finished.emit(False, "Lỗi xóa file gốc khi Cut!")
                        return
                        
                self.finished.emit(True, "Hoàn tất thao tác!")
        except Exception as e:
            self.finished.emit(False, str(e))

class DeleteWorker(QThread):
    progress = pyqtSignal(str)
    finished = pyqtSignal(bool, str)
    live_status = pyqtSignal(dict)

    def __init__(self, protocol, target_path):
        super().__init__()
        self.protocol = protocol
        self.target_path = target_path
        self.last_status_time = 0

    def run(self):
        try:
            success = self.delete_recursive(self.target_path)
            self.finished.emit(success, "Xóa hoàn tất!" if success else "Lỗi khi xóa đệ quy!")
        except Exception as e:
            self.finished.emit(False, str(e))

    def poll_status(self, force=False):
        if force or time.time() - self.last_status_time > 0.5:
            self.last_status_time = time.time()
            st = self.protocol.get_ui_live_status()
            if st:
                self.live_status.emit(st)

    def delete_recursive(self, path):
        self.progress.emit(f"Đang xóa: {path}")
        
        if self.protocol.delete_item(path):
            self.poll_status(force=True)
            return True
        
        files = self.protocol.open_dir(path)
        if files is None: 
            return False
            
        for f in files:
            child_path = posixpath.join(path, f['name'])
            if f['is_dir']:
                if not self.delete_recursive(child_path):
                    return False
            else:
                self.progress.emit(f"Đang xóa file: {child_path}")
                if not self.protocol.delete_item(child_path):
                    return False
                self.poll_status(force=True)
                    
        res = self.protocol.delete_item(path)
        self.poll_status(force=True)
        return res

class SDExplorerApp(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("STM32 SD Card Explorer")
        self.resize(900, 600)
        
        self.protocol = None 
        self.current_path = "/" 
        self.sd_clipboard = None
        
        self.init_ui() 
        self.setup_shortcuts() 
        
    def init_ui(self):
        toolbar = QToolBar("Navigation")
        toolbar.setMovable(False)
        self.addToolBar(Qt.TopToolBarArea, toolbar)
        
        self.txt_com = QLineEdit("COM9") 
        self.txt_com.setFixedWidth(80)
        
        self.btn_connect = QPushButton(" Kết nối ")
        self.btn_connect.clicked.connect(self.toggle_connection)
        
        icon_up = self.style().standardIcon(QStyle.SP_ArrowUp)
        icon_refresh = self.style().standardIcon(QStyle.SP_BrowserReload)
        icon_new_folder = self.style().standardIcon(QStyle.SP_FileDialogNewFolder) # Icon thư mục mới
        
        self.action_up = toolbar.addAction(icon_up, "Lên một cấp")
        self.action_up.triggered.connect(self.go_up)
        self.action_refresh = toolbar.addAction(icon_refresh, "Làm mới")
        self.action_refresh.triggered.connect(self.load_directory)
        
        toolbar.addSeparator()
        self.action_mkdir = toolbar.addAction(icon_new_folder, "Tạo thư mục")
        self.action_mkdir.triggered.connect(self.do_mkdir) # Nút tạo thư mục trên thanh công cụ
        
        toolbar.addSeparator() 
        toolbar.addWidget(QLabel("  COM Port: "))
        toolbar.addWidget(self.txt_com)
        toolbar.addWidget(self.btn_connect)
        
        toolbar.addWidget(QLabel("  Đường dẫn: "))
        self.txt_path = QLineEdit(self.current_path)
        self.txt_path.returnPressed.connect(self.load_directory) 
        toolbar.addWidget(self.txt_path)
        
        self.table = QTableWidget(0, 3) 
        self.table.setHorizontalHeaderLabels(["Tên File/Thư mục", "Kích thước", "Loại"]) 
        self.table.horizontalHeader().setSectionResizeMode(QHeaderView.Interactive)
        self.table.horizontalHeader().setStretchLastSection(False) 
        
        self.table.setColumnWidth(0, 450)
        self.table.setColumnWidth(1, 250) 
        self.table.setColumnWidth(2, 150)
        
        self.table.setShowGrid(False) 
        self.table.setAlternatingRowColors(True) 
        self.table.setSelectionBehavior(QTableWidget.SelectRows) 
        self.table.setSelectionMode(QTableWidget.SingleSelection) 
        self.table.verticalHeader().setVisible(False) 
        self.table.setEditTriggers(QTableWidget.NoEditTriggers) 
        
        self.table.setStyleSheet("""
            QTableWidget { background-color: #ffffff; border: none; font-size: 16px; }
            QTableWidget::item:selected { background-color: #cce8ff; color: #000000; }
            QHeaderView { background-color: #ffffff; border: none; border-bottom: 1px solid #e0e0e0; }
            QHeaderView::section {
                background-color: #ffffff;  
                border: none;               
                border-bottom: 1px solid #e0e0e0; 
                border-right: 1px solid #e0e0e0;  
                padding: 4px;               
                font-weight: bold;          
            }
        """)
        
        self.table.doubleClicked.connect(self.on_item_double_click)
        self.table.setContextMenuPolicy(Qt.CustomContextMenu) 
        self.table.customContextMenuRequested.connect(self.show_context_menu)
        
        self.setCentralWidget(self.table)
        
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        
        self.lbl_info = QLabel("Trạng thái: Chưa kết nối")
        self.status_bar.addWidget(self.lbl_info)
        
        self.lbl_speed = QLabel("")
        self.lbl_speed.setVisible(False)
        self.status_bar.addWidget(self.lbl_speed)
        
        self.progress_bar = QProgressBar()
        self.progress_bar.setFixedWidth(250)
        self.progress_bar.setValue(0)
        self.progress_bar.setVisible(False) 
        self.status_bar.addPermanentWidget(self.progress_bar)

    def setup_shortcuts(self):
        self.shortcut_paste = QShortcut(QKeySequence("Ctrl+V"), self)
        self.shortcut_paste.activated.connect(self.handle_paste)
        self.shortcut_copy = QShortcut(QKeySequence("Ctrl+C"), self)
        self.shortcut_copy.activated.connect(self.handle_copy)
        self.shortcut_cut = QShortcut(QKeySequence("Ctrl+X"), self)
        self.shortcut_cut.activated.connect(self.handle_cut)

    def toggle_connection(self):
        if self.protocol is None:
            try:
                self.protocol = SDCardProtocol(self.txt_com.text().strip())
                vol = self.protocol.get_vol_info() 
                if vol:
                    self.lbl_info.setText(f" Mạch: {vol['label']} | Dung lượng trống: {vol['free_kb']/1024:.1f} MB")
                    self.btn_connect.setText(" Ngắt kết nối ")
                    self.txt_com.setEnabled(False) 
                    self.load_directory() 
                else:
                    self.protocol.close()
                    self.protocol = None
                    QMessageBox.critical(self, "Lỗi", "Không nhận được phản hồi từ STM32!")
            except Exception as e:
                QMessageBox.critical(self, "Lỗi COM", f"Không thể mở cổng: {str(e)}")
        else:
            self.protocol.close()
            self.protocol = None
            self.btn_connect.setText(" Kết nối ")
            self.txt_com.setEnabled(True)
            self.lbl_info.setText("Trạng thái: Đã ngắt kết nối")
            self.table.setRowCount(0)

    def load_directory(self):
        if not self.protocol: return
        
        # 1. Lấy text từ thanh địa chỉ và dùng posixpath.normpath để gọt sạch dấu / thừa ở đuôi
        raw_path = self.txt_path.text().strip()
        if not raw_path: 
            raw_path = "/"
            
        self.current_path = posixpath.normpath(raw_path)
        
        # Normpath đôi khi biến "/" thành "\" hoặc chuỗi rỗng trên Windows, ta ép lại cho chắc
        if self.current_path in ["", ".", "\\"]: 
            self.current_path = "/"
            
        self.txt_path.setText(self.current_path)
        
        # Gọi STM32 quét file
        files = self.protocol.open_dir(self.current_path)
        self.table.setRowCount(0) 
        
        icon_dir = self.style().standardIcon(QStyle.SP_DirIcon)
        icon_file = self.style().standardIcon(QStyle.SP_FileIcon)
        
        for row, f in enumerate(files):
            self.table.insertRow(row)
            name_item = QTableWidgetItem(f['name'])
            name_item.setIcon(icon_dir if f['is_dir'] else icon_file)
            self.table.setItem(row, 0, name_item)
            size_str = "" if f['is_dir'] else f"{f['size']:,} KB"
            self.table.setItem(row, 1, QTableWidgetItem(size_str))
            self.table.setItem(row, 2, QTableWidgetItem("File folder" if f['is_dir'] else "File"))

    def go_up(self):
        # Dùng posixpath.dirname để lùi về 1 cấp thư mục cha một cách an toàn
        parent_dir = posixpath.dirname(self.current_path)
        self.txt_path.setText(parent_dir)
        self.load_directory()

    def on_item_double_click(self):
        row = self.table.currentRow()
        item_type = self.table.item(row, 2).text()
        name = self.table.item(row, 0).text()
        
        if item_type == "File folder":
            # Dùng posixpath.join để ghép đường dẫn mới (Tự động lo việc có hay ko dấu /)
            new_path = posixpath.join(self.current_path, name)
            self.txt_path.setText(new_path)
            self.load_directory()

    # CẬP NHẬT MENU CHUỘT PHẢI
    def show_context_menu(self, pos):
        if not self.protocol: return
        row = self.table.rowAt(pos.y()) 
        menu = QMenu()
        
        if row >= 0: 
            self.table.selectRow(row) 
            item_type = self.table.item(row, 2).text()
            name = self.table.item(row, 0).text()
            
            # Sử dụng posixpath để nối đường dẫn tuyệt đối chính xác
            target_path = posixpath.join(self.current_path, name)
            
            if item_type == "File folder":
                upload_action = menu.addAction(f"Tải file vào thư mục '{name}'...")
                if self.sd_clipboard:
                    paste_action = menu.addAction("Dán (Paste)")
                else:
                    paste_action = None
                menu.addSeparator()
                delete_action = menu.addAction("Xóa Thư mục")
                
                action = menu.exec_(self.table.viewport().mapToGlobal(pos))
                if action == upload_action: 
                    self.do_upload(target_path) 
                elif paste_action and action == paste_action:
                    self.do_sd_paste(target_path)
                elif action == delete_action: 
                    self.do_delete(target_path)
                    
            else: 
                download_action = menu.addAction("Tải xuống (Download)")
                copy_action = menu.addAction("Copy")
                cut_action = menu.addAction("Cut")
                delete_action = menu.addAction("Xóa File")
                menu.addSeparator()
                upload_action = menu.addAction("Tải file lên thư mục hiện tại...")
                
                action = menu.exec_(self.table.viewport().mapToGlobal(pos))
                if action == download_action: 
                    self.do_download(target_path, name)
                elif action == copy_action:
                    self.do_copy(target_path, name)
                elif action == cut_action:
                    self.do_cut(target_path, name)
                elif action == delete_action: 
                    self.do_delete(target_path)
                elif action == upload_action: 
                    self.do_upload(self.current_path) 
                
        else: 
            self.table.clearSelection() 
            upload_action = menu.addAction("Tải file lên thư mục hiện tại...")
            mkdir_action = menu.addAction("Tạo thư mục mới (New Folder)...")
            if self.sd_clipboard:
                paste_action = menu.addAction("Dán (Paste)")
            else:
                paste_action = None
            
            action = menu.exec_(self.table.viewport().mapToGlobal(pos))
            if action == upload_action: 
                self.do_upload(self.current_path)
            elif action == mkdir_action:
                self.do_mkdir()
            elif paste_action and action == paste_action:
                self.do_sd_paste(self.current_path)

    def handle_paste(self):
        if not self.protocol: return
        clipboard = QApplication.clipboard()
        mime_data = clipboard.mimeData()
        
        if mime_data.hasUrls() and mime_data.urls():
            local_path = mime_data.urls()[0].toLocalFile() 
            if os.path.isfile(local_path): 
                filename = os.path.basename(local_path)
                remote_path = posixpath.join(self.current_path, filename)
                
                reply = QMessageBox.question(self, 'Dán File', f'Bạn muốn tải file:\n"{filename}"\nvào thư mục thẻ nhớ hiện tại?', QMessageBox.Yes | QMessageBox.No)
                if reply == QMessageBox.Yes:
                    self.start_transfer("UPLOAD", local_path, remote_path)
            else:
                QMessageBox.warning(self, "Chưa hỗ trợ", "Chỉ hỗ trợ Paste File, không hỗ trợ Paste Thư mục!")
        elif self.sd_clipboard:
            self.do_sd_paste(self.current_path)

    def handle_copy(self):
        if not self.protocol: return
        row = self.table.currentRow()
        if row >= 0:
            item_type = self.table.item(row, 2).text()
            name = self.table.item(row, 0).text()
            if item_type == "File":
                target_path = posixpath.join(self.current_path, name)
                self.do_copy(target_path, name)

    def handle_cut(self):
        if not self.protocol: return
        row = self.table.currentRow()
        if row >= 0:
            item_type = self.table.item(row, 2).text()
            name = self.table.item(row, 0).text()
            if item_type == "File":
                target_path = posixpath.join(self.current_path, name)
                self.do_cut(target_path, name)

    def do_copy(self, path, name):
        self.sd_clipboard = {"path": path, "name": name, "action": "COPY_SD_TO_SD"}
        self.lbl_info.setText(f"Đã copy: {name}")

    def do_cut(self, path, name):
        self.sd_clipboard = {"path": path, "name": name, "action": "CUT_SD_TO_SD"}
        self.lbl_info.setText(f"Đã cắt: {name}")

    def do_sd_paste(self, target_dir):
        if not self.sd_clipboard: return
        src_path = self.sd_clipboard["path"]
        src_name = self.sd_clipboard["name"]
        action = self.sd_clipboard["action"]
        
        dest_path = posixpath.join(target_dir, src_name)
        if src_path == dest_path:
            QMessageBox.warning(self, "Lỗi", "Nguồn và đích trùng nhau!")
            return
            
        self.start_transfer(action, src_path, dest_path)
        if action == "CUT_SD_TO_SD":
            self.sd_clipboard = None

    # API TẠO THƯ MỤC TRÊN GIAO DIỆN
    def do_mkdir(self):
        if not self.protocol:
            return
            
        folder_name, ok = QInputDialog.getText(self, 'Tạo thư mục', 'Nhập tên thư mục mới:')
        if ok and folder_name.strip():
            # Dùng posixpath để ghép đường dẫn an toàn
            new_path = posixpath.join(self.current_path, folder_name.strip())
            
            if self.protocol.create_dir(new_path):
                self.load_directory()
            else:
                QMessageBox.warning(self, "Lỗi", "Không thể tạo thư mục! (Có thể do trùng tên, sai ký tự hoặc thẻ bị lỗi)")

    def do_delete(self, path):
        reply = QMessageBox.question(self, 'Xác nhận', f"Xóa vĩnh viễn '{path}' và toàn bộ nội dung bên trong?", QMessageBox.Yes | QMessageBox.No)
        if reply == QMessageBox.Yes:
            self.progress_bar.setRange(0, 0)
            self.progress_bar.setVisible(True)
            self.table.setEnabled(False)
            self.lbl_info.setText("Đang xóa...")
            
            self.live_dialog = LiveStatusDialog(self)
            self.live_dialog.log(f"PC: Bắt đầu DELETE đệ quy '{path}'...")
            self.live_dialog.show()
            
            self.del_worker = DeleteWorker(self.protocol, path)
            self.del_worker.progress.connect(self.delete_progress)
            self.del_worker.live_status.connect(self.live_dialog.update_stm_status)
            self.del_worker.finished.connect(self.delete_done)
            self.del_worker.start()

    def delete_progress(self, msg):
        self.lbl_info.setText(msg)
        if hasattr(self, 'live_dialog') and self.live_dialog:
            self.live_dialog.update_pc_text(msg)

    def delete_done(self, success, msg):
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setVisible(False)
        self.table.setEnabled(True)
        if hasattr(self, 'live_dialog') and self.live_dialog:
            self.live_dialog.finish(msg)
            
        if success:
            self.lbl_info.setText("Xóa hoàn tất!")
            self.load_directory()
        else:
            self.lbl_info.setText("Lỗi khi xóa!")
            QMessageBox.critical(self, "Lỗi", msg)

    def do_download(self, remote_path, default_name):
        local_path, _ = QFileDialog.getSaveFileName(self, "Lưu file", default_name)
        if local_path:
            self.start_transfer("DOWNLOAD", local_path, remote_path)

    # Cập nhật thuật toán ghép chuỗi Upload siêu an toàn
    def do_upload(self, target_dir=None):
        if target_dir is None:
            target_dir = self.current_path
            
        local_path, _ = QFileDialog.getOpenFileName(self, "Chọn file tải lên")
        if local_path:
            filename = os.path.basename(local_path)
            
            # Sử dụng posixpath.join để đảm bảo cấu trúc / luôn đúng
            # Ví dụ: posixpath.join("/System", "test.txt") -> "/System/test.txt"
            # posixpath.join("/", "test.txt") -> "/test.txt"
            remote_path = posixpath.join(target_dir, filename) 
            
            self.start_transfer("UPLOAD", local_path, remote_path)

    def start_transfer(self, task_type, local_path, remote_path):
        self.progress_bar.setValue(0)
        self.progress_bar.setVisible(True)
        self.lbl_speed.setText("Đang tính tốc độ...")
        self.lbl_speed.setVisible(True)
        self.table.setEnabled(False)
        self.lbl_info.setText(f"Đang {task_type.lower()}...")
        
        self.transfer_start_time = time.time()
        self.current_task_type = task_type
        self.current_filename = os.path.basename(remote_path)
        
        self.live_dialog = LiveStatusDialog(self)
        self.live_dialog.log(f"PC: Bắt đầu {task_type}...")
        self.live_dialog.show()
        
        self.worker = TransferWorker(self.protocol, task_type, local_path, remote_path)
        self.worker.progress.connect(self.update_progress)
        self.worker.live_status.connect(self.live_dialog.update_stm_status)
        self.worker.finished.connect(self.transfer_done)
        self.worker.start()

    def update_progress(self, current, total):
        if total > 0:
            percent = int((current / total) * 100) 
            self.progress_bar.setValue(percent) 
            
            elapsed = time.time() - getattr(self, 'transfer_start_time', time.time())
            speed = 0
            if elapsed > 0.5:
                speed = (current / 1024) / elapsed
                self.lbl_speed.setText(f" {speed:.1f} KB/s ")
                
            if hasattr(self, 'live_dialog') and self.live_dialog:
                self.live_dialog.update_pc_status(percent, speed, getattr(self, 'current_task_type', ''), getattr(self, 'current_filename', ''))

    def transfer_done(self, success, msg):
        self.progress_bar.setVisible(False)
        self.lbl_speed.setVisible(False)
        self.table.setEnabled(True)
        
        if hasattr(self, 'live_dialog') and self.live_dialog:
            self.live_dialog.finish(msg)
            
        if success:
            self.lbl_info.setText("Hoàn tất truyền file!")
            self.load_directory() 
        else:
            self.lbl_info.setText("Lỗi truyền file!")
            QMessageBox.critical(self, "Lỗi", msg)

if __name__ == '__main__':
    app = QApplication(sys.argv) 
    app.setStyle("Fusion")       
    ex = SDExplorerApp()         
    ex.show()                    
    sys.exit(app.exec_())