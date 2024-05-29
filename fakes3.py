import http.server
import socketserver
import ssl

import logging

logging.basicConfig(level=logging.INFO)

class YesMan(http.server.SimpleHTTPRequestHandler):
    def say_yes(self):
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"OK")

    def do_GET(self):
        logging.info(f"GET {self.client_address}")
        say_yes()
        
    def do_POST(self):
        logging.info(f"POST {self.client_address}")
        say_yes()
    
    def do_PUT(self):
        logging.info(f"PUT {self.client_address}")
        say_yes()
    
    def do_DELETE(self):
        logging.info(f"DELETE {self.client_address}")
        say_yes()

PORT    = 443
use_ssl = True

with socketserver.TCPServer(("", PORT), YesMan) as httpd:
    if use_ssl:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(keyfile="key.pem", certfile="cert.pem")
        httpd.socket = context.wrap_socket(httpd.socket, server_side = True)

    print(f"Serving on port {PORT}")
    httpd.serve_forever()
