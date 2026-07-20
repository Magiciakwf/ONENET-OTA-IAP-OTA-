import binascii
import os
import struct
import time

import serial


SERIAL_PORT = "COM4"
BAUD_RATE = 115200
FILE_PATH = "STM32.bin"
PACKET_SIZE = 256

FRAME_HEADER = 0xA5
CMD_START = 0x01
CMD_DATA = 0x02
CMD_END = 0x03
CMD_RESPONSE = 0x80

STATUS_OK = 0x00
MAX_RETRIES = 3

STATUS_TEXT = {
    0x01: "ZET6 OTA state error",
    0x02: "data packet sequence error",
    0x03: "Flash erase/write error",
    0x04: "invalid frame or data length",
    0x05: "firmware CRC32 mismatch",
    0x06: "received firmware size mismatch",
    0x07: "ZET6 OTA queue is full",
}


def send_packet(ser, cmd, data=b""):
    """Send [A5][CMD][LEN_H][LEN_L][PAYLOAD]."""
    if len(data) > 0xFFFF:
        raise ValueError("payload is too large")
    ser.write(struct.pack(">BBH", FRAME_HEADER, cmd, len(data)) + data)
    ser.flush()


def read_exactly(ser, size, deadline):
    data = bytearray()
    while len(data) < size:
        if time.monotonic() >= deadline:
            raise TimeoutError
        chunk = ser.read(size - len(data))
        if chunk:
            data.extend(chunk)
    return bytes(data)


def read_response(ser, timeout):
    """Read one response frame; unrelated/debug bytes before A5 are skipped."""
    deadline = time.monotonic() + timeout

    while time.monotonic() < deadline:
        marker = ser.read(1)
        if not marker or marker[0] != FRAME_HEADER:
            continue

        header = read_exactly(ser, 3, deadline)
        cmd, length = struct.unpack(">BH", header)
        payload = read_exactly(ser, length, deadline)
        if cmd == CMD_RESPONSE and length == 4:
            acked_cmd, status, sequence = struct.unpack(">BBH", payload)
            return acked_cmd, status, sequence

    raise TimeoutError


def send_and_wait_ack(ser, cmd, data, sequence, timeout):
    """Stop-and-wait: retry only when an ACK is lost or times out."""
    for attempt in range(1, MAX_RETRIES + 1):
        send_packet(ser, cmd, data)

        try:
            while True:
                acked_cmd, status, ack_sequence = read_response(ser, timeout)
                if acked_cmd != cmd or ack_sequence != sequence:
                    continue
                if status != STATUS_OK:
                    reason = STATUS_TEXT.get(status, f"unknown status 0x{status:02X}")
                    raise RuntimeError(
                        f"device rejected CMD=0x{cmd:02X}, seq={sequence}: {reason}"
                    )
                return
        except TimeoutError:
            if attempt == MAX_RETRIES:
                raise RuntimeError(
                    f"ACK timeout: CMD=0x{cmd:02X}, seq={sequence}, "
                    f"retried {MAX_RETRIES} times"
                )
            print(
                f"ACK timeout, retrying CMD=0x{cmd:02X}, "
                f"seq={sequence} ({attempt}/{MAX_RETRIES})"
            )


def main():
    if not os.path.exists(FILE_PATH):
        print(f"Error: file not found: {FILE_PATH}")
        return

    with open(FILE_PATH, "rb") as firmware_file:
        file_content = firmware_file.read()

    file_size = len(file_content)
    file_crc = binascii.crc32(file_content) & 0xFFFFFFFF
    total_chunks = (file_size + PACKET_SIZE - 1) // PACKET_SIZE

    if file_size == 0:
        print("Error: firmware file is empty")
        return
    if total_chunks > 0xFFFF:
        print("Error: too many data packets for the 16-bit sequence number")
        return

    try:
        ser = serial.Serial(
            SERIAL_PORT,
            BAUD_RATE,
            timeout=0.1,
            write_timeout=2,
        )
    except Exception as exc:
        print(f"Error: could not open {SERIAL_PORT}: {exc}")
        return

    try:
        ser.reset_input_buffer()
        print(
            f"Firmware: size={file_size} bytes, CRC32=0x{file_crc:08X}, "
            f"packets={total_chunks}"
        )

        print("START sent; waiting for ZET6 Flash erase ACK...")
        send_and_wait_ack(
            ser,
            CMD_START,
            struct.pack(">I", file_size),
            sequence=0,
            timeout=15.0,
        )
        print("Flash erase completed.")

        sent_bytes = 0
        for sequence in range(total_chunks):
            offset = sequence * PACKET_SIZE
            chunk = file_content[offset : offset + PACKET_SIZE]
            payload = struct.pack(">H", sequence) + chunk
            send_and_wait_ack(
                ser,
                CMD_DATA,
                payload,
                sequence=sequence,
                timeout=3.0,
            )

            sent_bytes += len(chunk)
            print(
                f"Progress: {sent_bytes}/{file_size} "
                f"({sent_bytes / file_size * 100:.1f}%)"
            )

        print("END sent; waiting for size/CRC verification ACK...")
        send_and_wait_ack(
            ser,
            CMD_END,
            struct.pack(">I", file_crc),
            sequence=total_chunks,
            timeout=10.0,
        )
        print("OTA completed successfully; ZET6 is rebooting.")
    except (RuntimeError, serial.SerialException) as exc:
        print(f"OTA failed: {exc}")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
