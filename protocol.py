import serial
import struct
import time
import os

# ==============================================================================
# MÃ LỆNH (COMMAND DICTIONARY)
# ==============================================================================
PID_CMD  = 0x01
PID_DATA = 0x02
PID_ACK  = 0x07
PID_END  = 0x08

CMD_SYS_PING_REQ         = 0x00
CMD_GET_VOL_INFO_REQ     = 0x10
CMD_DIR_OPEN_REQ         = 0x11
CMD_FILE_DELETE_REQ      = 0x12
CMD_DIR_CREATE_REQ       = 0x13 # Mã lệnh tạo thư mục
CMD_FILE_READ_REQ        = 0x03
CMD_FILE_READ_NEXT_REQ   = 0x04
CMD_FILE_WRITE_START_REQ = 0x14
CMD_FILE_WRITE_DATA_REQ  = 0x15
CMD_FILE_WRITE_END_REQ   = 0x16
CMD_GET_UI_STATUS_REQ    = 0x50

class SDCardProtocol:
    def __init__(self, port, baudrate=2000000, timeout=2.0):
        self.ser = serial.Serial(port, baudrate, timeout=timeout)
        self.ser.set_buffer_size(rx_size=262144, tx_size=262144) 
        self.CHUNK_SIZE = 1024 
        self.rx_buf = bytearray() 

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def build_frame(self, pid, cmd_code, payload=b''):
        header   = b'\x55\xAA'
        addr     = b'\xFF\xFF\xFF\xFF' 
        length   = len(payload) + 3
        
        total = pid + ((length >> 8) & 0xFF) + (length & 0xFF) + cmd_code + sum(payload)
        checksum = total & 0xFFFF
        
        return (header + addr
                + bytes([pid, (length >> 8) & 0xFF, length & 0xFF, cmd_code])
                + payload
                + struct.pack('>H', checksum))

    def read_frame(self, timeout=5.0, ignore_ui_status=True):
        start_time = time.time()
        while True:
            sync_idx = self.rx_buf.find(b'\x55\xAA')
            if sync_idx != -1:
                del self.rx_buf[:sync_idx]
                if len(self.rx_buf) >= 9:
                    length = (self.rx_buf[7] << 8) | self.rx_buf[8] 
                    total_frame_len = 9 + length 
                    if len(self.rx_buf) >= total_frame_len:
                        frame_data = bytes(self.rx_buf[:total_frame_len])
                        del self.rx_buf[:total_frame_len] 

                        pid = frame_data[6]
                        cmd_byte = frame_data[9]
                        payload  = frame_data[10:-2]
                        
                        # Bỏ qua các frame UI Status bị kẹt trong buffer nếu không phải là hàm lấy UI
                        if ignore_ui_status and cmd_byte == 0xB0:
                            start_time = time.time()
                            continue
                            
                        return pid, cmd_byte, payload

            waiting = self.ser.in_waiting
            if waiting > 0:
                self.rx_buf.extend(self.ser.read(waiting))
            else:
                time.sleep(0.001)

            if (time.time() - start_time) > timeout:
                return None, None, None

    # ================= CÁC API THỰC THI =================
    
    # API: TẠO THƯ MỤC MỚI (Bổ sung thêm)
    def create_dir(self, path):
        path_payload = path.encode('utf-8') + b'\x00'
        self.ser.write(self.build_frame(PID_CMD, CMD_DIR_CREATE_REQ, path_payload))
        pid, cmd, payload = self.read_frame()
        # Nếu nhận được ACK (0xAF) và Status = 0 (FR_OK) thì thành công
        return cmd == 0xAF and (not payload or payload[0] == 0)

    def get_ui_live_status(self):
        self.ser.write(self.build_frame(PID_CMD, CMD_GET_UI_STATUS_REQ))
        pid, cmd, payload = self.read_frame(timeout=1.0, ignore_ui_status=False)
        if cmd == 0xB0 and payload and len(payload) >= 81:
            unpacked = struct.unpack('<B B B B 64s I I B f', payload[:81])
            return {
                "is_mounted": unpacked[0],
                "op_type": unpacked[1],
                "transfer_status": unpacked[2],
                "last_error": unpacked[3],
                "filename": unpacked[4].decode('utf-8', 'ignore').strip('\x00'),
                "total_bytes": unpacked[5],
                "bytes_processed": unpacked[6],
                "progress_percent": unpacked[7],
                "speed_kbps": unpacked[8]
            }
        return None

    def get_vol_info(self):
        self.ser.write(self.build_frame(PID_CMD, CMD_GET_VOL_INFO_REQ))
        pid, cmd, payload = self.read_frame()
        if cmd == 0xA0 and payload: 
            unpacked = struct.unpack('<B I I 12s B H H I I I', payload[:38])
            return {
                "status": unpacked[0],
                "total_kb": unpacked[1],
                "free_kb": unpacked[2],
                "label": unpacked[3].decode('utf-8', 'ignore').strip('\x00')
            }
        return None

    def open_dir(self, path):
        path_payload = path.encode() + b'\x00'
        self.ser.write(self.build_frame(PID_CMD, CMD_DIR_OPEN_REQ, path_payload))
        
        files = []
        while True:
            pid, cmd, payload = self.read_frame()
            if cmd == 0xA1:
                if len(payload) >= 151:
                    fsize   = struct.unpack_from('<I', payload, 0)[0]
                    fattrib = payload[8]
                    fname   = payload[9:137].rstrip(b'\x00').decode('utf-8', 'ignore')
                    
                    if fname not in ['.', '..']: 
                        files.append({
                            "name": fname,
                            "size": fsize,
                            "is_dir": bool(fattrib & 0x10) 
                        })
            elif cmd == 0xA2:
                break
            elif not cmd:
                break 
        return files

    def delete_item(self, path):
        path_payload = path.encode() + b'\x00'
        self.ser.write(self.build_frame(PID_CMD, CMD_FILE_DELETE_REQ, path_payload))
        pid, cmd, payload = self.read_frame()
        return cmd == 0xAF and (payload[0] == 0 if payload else False)

    def download_file(self, remote_path, local_path, progress_cb=None):
        self.ser.reset_input_buffer()
        path_payload = remote_path.encode() + b'\x00'
        self.ser.write(self.build_frame(PID_CMD, CMD_FILE_READ_REQ, path_payload))
        
        pid, cmd, payload = self.read_frame()
        if cmd != 0xA3:
            self.last_error = f"CMD mismatch {cmd}"
            return False
        if payload[0] != 0:
            self.last_error = f"FATFS Code {payload[0]}"
            return False 
        file_size = struct.unpack_from('<I', payload, 1)[0]
        
        bytes_read = 0
        with open(local_path, 'wb') as f:
            self.ser.write(self.build_frame(PID_CMD, CMD_FILE_READ_NEXT_REQ))
            while True:
                pid, cmd, payload = self.read_frame()
                if cmd == 0x90: 
                    f.write(payload)
                    bytes_read += len(payload)
                    if progress_cb: progress_cb(bytes_read, file_size)
                    self.ser.write(self.build_frame(PID_CMD, CMD_FILE_READ_NEXT_REQ))
                elif cmd == 0xA4: 
                    break 
                else:
                    return False 
        return True

    def upload_file(self, local_path, remote_path, progress_cb=None):
        self.ser.reset_input_buffer()
        file_size = os.path.getsize(local_path)
        path_payload = struct.pack('<I', file_size) + remote_path.encode() + b'\x00'
        self.ser.write(self.build_frame(PID_CMD, CMD_FILE_WRITE_START_REQ, path_payload))
        
        pid, cmd, payload = self.read_frame()
        if cmd != 0xAF:
            self.last_error = f"CMD mismatch {cmd}"
            return False
        if payload and payload[0] != 0:
            self.last_error = f"FATFS Code {payload[0]}"
            return False 
        
        file_size = os.path.getsize(local_path)
        bytes_written = 0
        
        with open(local_path, 'rb') as f:
            while True:
                chunk = f.read(self.CHUNK_SIZE) 
                if not chunk: break 
                
                self.ser.write(self.build_frame(PID_CMD, CMD_FILE_WRITE_DATA_REQ, chunk))
                pid, cmd, payload = self.read_frame()
                if cmd != 0xAF or (payload and payload[0] != 0): return False 
                
                bytes_written += len(chunk)
                if progress_cb: progress_cb(bytes_written, file_size)
        
        self.ser.write(self.build_frame(PID_CMD, CMD_FILE_WRITE_END_REQ))
        pid, cmd, payload = self.read_frame()
        return cmd == 0xAF and (not payload or payload[0] == 0)