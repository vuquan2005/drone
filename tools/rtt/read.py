#!/usr/bin/env python3
"""Forward one existing OpenOCD RTT TCP stream to stdout, without decoding."""

import argparse
import socket
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9091, help="9091: data; 9090: standalone log")
    args = parser.parse_args()
    if not 0 < args.port < 65536:
        parser.error("Port must be between 1 and 65535")
    try:
        with socket.create_connection((args.host, args.port), timeout=5) as connection:
            connection.settimeout(None)
            while True:
                data = connection.recv(4096)
                if not data:
                    break
                # Extension point: decode application records here, or pipe to
                # a separate plotting tool. Preserve arbitrary bytes for now.
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
    except (KeyboardInterrupt, BrokenPipeError):
        return 0
    except OSError as error:
        print(f"RTT: {error}. Start OpenOCD/debug first and check the TCP port.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
