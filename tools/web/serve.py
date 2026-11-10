#!/usr/bin/env python3
# Copyright(c) 2015-2026 Panos Karabelas
# Licensed under the Spartan Engine License. See license.md in the repository root.
# https://github.com/PanosK92/SpartanEngine/blob/master/license.md
# Commercial use requires written permission and negotiated payment terms.

"""Serve the Spartan web package from this folder, then open the printed address in a WebGPU browser."""

import http.server
import os
import sys
import webbrowser


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        # cross origin isolation keeps SharedArrayBuffer and high resolution timers available
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    Handler.extensions_map[".wasm"] = "application/wasm"
    url = f"http://localhost:{port}/spartan.html"
    print(f"serving on {url}, ctrl+c stops", flush=True)
    webbrowser.open(url)
    http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()


if __name__ == "__main__":
    main()
