#!/usr/bin/env python3

# Copyright (c) 2025 muzkr
#
#   https://github.com/muzkr
#
# Licensed under the MIT License (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at the root of this repository.
#
#     Unless required by applicable law or agreed to in writing, software
#     distributed under the License is distributed on an "AS IS" BASIS,
#     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#     See the License for the specific language governing permissions and
#     limitations under the License.
#


import argparse
import signal
from time import sleep
import os

import _fwcodec as fc
import _link as ll


def load_image(file: str) -> bytes:

    a = bytearray()
    with open(file, "rb") as fd:
        buf = bytearray(512)
        while True:
            len1 = fd.readinto(buf)
            if len1 > 0:
                a.extend(memoryview(buf)[:len1])
            if len1 < len(buf):
                break

    return a


def main_dump(args, ser):

    import _dump as dd

    dump_file: str = args.file

    print("Dump file: {}".format(dump_file))
    if os.path.exists(dump_file):
        print("Dump file exists. Will be overwritten")

    if args.config:
        dump_what = dd.DUMP_CONFIG
        print("Dump configuration..")
    elif args.calib:
        dump_what = dd.DUMP_CALIB
        print("Dump calibration data..")
    else:
        dump_what = dd.DUMP_ALL
        print("Dump all..")

    quit_flag = False

    def quit_handler(sig, frame):
        nonlocal quit_flag
        quit_flag = True

    signal.signal(signal.SIGINT, quit_handler)

    dump = dd.EepromDump(ser, dump_what, dump_file)
    while (not quit_flag) and dump.loop():
        sleep(0)


def main_restore(args, ser):

    import _dump as dd
    import _restore as rr

    dump_file: str = args.file

    print("Dump file: {}".format(dump_file))
    if not os.path.exists(dump_file):
        print("Dump file not exist")
        return

    if args.config:
        dump_what = dd.DUMP_CONFIG
        print("Restore configuration..")
    elif args.calib:
        dump_what = dd.DUMP_CALIB
        print("Restore calibration data..")
    else:
        dump_what = dd.DUMP_ALL
        print("Restore all..")

    quit_flag = False

    def quit_handler(sig, frame):
        nonlocal quit_flag
        quit_flag = True

    signal.signal(signal.SIGINT, quit_handler)

    dump = rr.EepromDump(ser, dump_what, dump_file)
    while (not quit_flag) and dump.loop():
        sleep(0)


def main_flash(args, ser):

    import _prog as pp

    bl_ver: str = args.bl_ver
    fw_file: str = args.file

    import _prog as pp_limits  # for BL_VER_LEN

    try:
        fw_image = load_image(fw_file)
        if 0 == len(fw_image):
            print("Invalid firmware image: {}: empty file".format(fw_file))
            return
    except Exception as e:
        print("Cannot load firmware image '{}': {}".format(fw_file, e))
        return

    if bl_ver is not None and len(bl_ver) > pp_limits.BL_VER_LEN:
        print(
            "Invalid bootloader version '{}': more than {} characters".format(
                bl_ver, pp_limits.BL_VER_LEN
            )
        )
        return

    print("Firmware image loaded: {}, size = {}".format(fw_file, len(fw_image)))

    quit_flag = False

    def quit_handler(sig, frame):
        nonlocal quit_flag
        quit_flag = True

    signal.signal(signal.SIGINT, quit_handler)

    prog = pp.Programmer(ser, fw_image, bl_ver)

    while (not quit_flag) and prog.loop():
        sleep(0)


def get_raw_output_path(file: str) -> str:

    root, ext = os.path.splitext(file)
    if ext:
        file_name = "{}.raw{}".format(os.path.basename(root), ext)
        candidate = "{}.raw{}".format(root, ext)
    else:
        file_name = os.path.basename(file) + ".raw.bin"
        candidate = file + ".raw.bin"

    out_dir = os.path.dirname(candidate) or "."
    if os.access(out_dir, os.W_OK):
        return candidate

    return os.path.join(os.getcwd(), file_name)


def main_decode(args):

    fw_file: str = args.file
    out_file: str = args.output or get_raw_output_path(fw_file)

    try:
        fw_image = load_image(fw_file)
        if 0 == len(fw_image):
            print("Invalid firmware image: {}: empty file".format(fw_file))
            return
    except Exception as e:
        print("Cannot load firmware image '{}': {}".format(fw_file, e))
        return

    if fc.is_raw_image(fw_image):
        print("Firmware image already looks raw")
        raw_image = fw_image
        version = None
    else:
        try:
            raw_image, version = fc.decode_packed_image(fw_image)
        except Exception as e:
            print("Cannot decode firmware image '{}': {}".format(fw_file, e))
            return

    with open(out_file, "wb") as fd:
        fd.write(raw_image)

    print("Raw firmware image written: {}, size = {}".format(out_file, len(raw_image)))
    if version:
        print("Packed version field: {}".format(version))


def add_port_args(parser):
    """Port options shared by the flash, dump and restore subcommands."""

    parser.add_argument(
        "--port", "-p", help="serial port, eg., '/dev/ttyUSB0'", required=True
    )
    parser.add_argument(
        "--dtr",
        choices=("0", "1"),
        help="force the DTR line low or high. pyserial raises it by default, "
        "which does not suit every cable",
    )
    parser.add_argument(
        "--rts", choices=("0", "1"), help="force the RTS line low or high"
    )
    parser.add_argument(
        "--settle",
        type=float,
        default=ll.SETTLE,
        help="seconds to wait after opening the port before transmitting "
        "(default {})".format(ll.SETTLE),
    )
    parser.add_argument(
        "--retries",
        type=int,
        default=ll.ATTEMPTS,
        help="how many times to resend an unanswered request before giving up "
        "(default {})".format(ll.ATTEMPTS),
    )
    parser.add_argument(
        "--wait",
        type=float,
        default=ll.WAIT,
        help="seconds to wait for messages the device sends unprompted "
        "(default {:.0f})".format(ll.WAIT),
    )


def main():

    # Usage:
    # serialtool.py --port <port> subcmd ..
    # serialtool.py .. flash [--bl-ver <ver>] <file>
    # serialtool.py .. dump {--config | --calib [| --all]} file
    # serialtool.py .. restore {--config | --calib [| --all]} file
    # serialtool.py decode <packed.bin> [raw.bin]
    ap = argparse.ArgumentParser(description="UV-K5 V2 serial tool")

    # TODO: have to add option to each of subcommands ??
    # ap.add_argument(
    #     "--port", "-p", help="serial port, eg., '/dev/ttyUSB0'", required=False
    # )
    sp = ap.add_subparsers(required=True, dest="subcommand")

    ap_flash = sp.add_parser("flash", help="flash firmware")
    add_port_args(ap_flash)
    ap_flash.add_argument(
        "--bl-ver",
        help="bootloader version to report to the device, eg. '7.00.07'. Max 16 "
        "characters. Default: whatever version the device announces. '*' sends "
        "an empty field",
        required=False,
        default=None,
    )
    ap_flash.add_argument("file", help="firmware image file")

    ap_dump = sp.add_parser("dump", help="dump configuration or calibration data")
    add_port_args(ap_dump)
    ag = ap_dump.add_mutually_exclusive_group()
    ag.add_argument("--config", action="store_true", help="dump configuration")
    ag.add_argument("--calib", action="store_true", help="dump calibration data")
    ag.add_argument(
        "--all",
        "-a",
        action="store_true",
        help="dump both configuration and calibration data. This is default",
    )
    ap_dump.add_argument("file", help="output dump file")

    ap_restore = sp.add_parser(
        "restore", help="restore configuration or calibration data from previous dump"
    )
    add_port_args(ap_restore)
    ag = ap_restore.add_mutually_exclusive_group()
    ag.add_argument("--config", action="store_true", help="restore configuration")
    ag.add_argument("--calib", action="store_true", help="restore calibration data")
    ag.add_argument(
        "--all",
        "-a",
        action="store_true",
        help="restore both configuration and calibration data. This is default",
    )
    ap_restore.add_argument("file", help="input dump file")

    ap_decode = sp.add_parser(
        "decode", help="decode a packed Quansheng stock firmware into a raw image"
    )
    ap_decode.add_argument("file", help="input firmware image file")
    ap_decode.add_argument(
        "output",
        nargs="?",
        help="output raw firmware image file (default: <input>.raw.bin)",
    )

    args = ap.parse_args()
    sub_name: str = args.subcommand

    print(ap.description)
    # print("Press Ctrl-C to quit")

    if "decode" == sub_name:
        main_decode(args)
        return

    port: str = args.port

    ll.configure(attempts=args.retries, wait=args.wait)

    try:
        ser = ll.open_port(
            port,
            settle=args.settle,
            dtr=None if args.dtr is None else "1" == args.dtr,
            rts=None if args.rts is None else "1" == args.rts,
        )
    except Exception as e:
        print("Cannot open port '{}': {}".format(port, e))
        return

    match sub_name:
        case "flash":
            main_flash(args, ser)
        case "dump":
            main_dump(args, ser)
        case "restore":
            main_restore(args, ser)

    ser.close()
    print("Quit")


if __name__ == "__main__":
    main()
