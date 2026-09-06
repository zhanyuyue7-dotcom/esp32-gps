"""Package the SHA256-locked, previously flashed firmware; never rebuild it implicitly."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile


def package(root):
    root = Path(root).resolve()
    manifest = json.loads((root / "delivery.json").read_text(encoding="utf-8"))
    name, version = manifest["name"], manifest["version"]
    if not all(re.fullmatch(r"[A-Za-z0-9._-]+", item) for item in (name, version)):
        raise ValueError("invalid package name or version")
    if manifest["chip"] != "esp32s3":
        raise ValueError("unsupported chip")
    parts = sorted(manifest["files"], key=lambda part: part["offset"])
    if [part["offset"] for part in parts] != [0, 0x8000, 0x10000]:
        raise ValueError("expected ESP32-S3 bootloader, partition table and factory app")
    payloads = {}
    rows, flash = [], []
    for part in parts:
        path = (root / part["path"]).resolve()
        if not path.is_relative_to(root):
            raise ValueError("firmware path outside project")
        if path.suffix != ".bin" or path.name in payloads:
            raise ValueError("expected unique BIN filenames")
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if not data or digest != part["sha256"]:
            raise ValueError("SHA256 mismatch: " + part["path"])
        payloads[path.name] = data
        rows.append(f"| {path.name} | {hex(part['offset'])} | {len(data)} | {digest} |")
        flash.append(f"{hex(part['offset'])} {path.name}")
    manual = (root / "docs/wiring.md").read_text(encoding="utf-8").rstrip()
    manual += "\n\n## 本交付包烧录说明\n\n"
    manual += f"交付标识：{name} / {version}。固件来源及校验见仓库 delivery.json；本包保留已成功烧录的历史 BIN，CI 重新编译不自动替换它。\n\n"
    manual += "目标仅限 ESP32-S3、16 MB Flash；不是其他 ESP32 型号的通用固件。先断电接线，核对开发板与全部引脚。\n\n"
    manual += "安装 Python 和 esptool 4.x 后，在解压目录执行（将 COMx 换成当前串口，不要选蓝牙端口）：\n\n"
    manual += "```powershell\npython -m pip install esptool==4.11.0\n"
    manual += "python -m esptool --chip esp32s3 --port COMx --baud 460800 write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m "
    manual += " ".join(flash) + "\n```\n\n"
    manual += "三个 BIN 缺一不可，不需要源码或 ESP-IDF。必须看到 Hash of data verified，再检查重启与外设。首次切换其他项目时先备份已有 NVS/数据；不要默认执行 erase_flash。更换分区布局可能使旧数据不可用。自动进入下载模式失败时才使用开发板 BOOT + RST/EN，不是外接功能按键。\n\n"
    manual += "| 文件 | 地址 | 字节数 | SHA256 |\n|---|---|---|---|\n" + "\n".join(rows)
    manual += "\n\n## 字体版权与许可\n\n固件包含 Atkinson Hyperlegible 字体点阵子集。以下保留随字体提供的完整许可证：\n\n"
    manual += (root / "third_party/atkinson-hyperlegible/OFL.txt").read_text(encoding="utf-8")
    payloads["接线手册.md"] = manual.replace("\r\n", "\n").encode("utf-8")
    output = root / "dist" / f"{name}-{version}.zip"
    output.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zipped:
        for filename, data in sorted(payloads.items()):
            entry = zipfile.ZipInfo(filename, date_time=(2026, 9, 6, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            zipped.writestr(entry, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    with zipfile.ZipFile(output) as zipped:
        if set(zipped.namelist()) != set(payloads) or zipped.testzip() is not None:
            raise ValueError("ZIP validation failed")
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    result = package(args.root)
    print(str(result))
    print("SHA256 " + hashlib.sha256(result.read_bytes()).hexdigest())

