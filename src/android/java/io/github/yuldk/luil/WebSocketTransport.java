package io.github.yuldk.luil;

import android.security.NetworkSecurityPolicy;
import java.io.ByteArrayOutputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.URI;
import java.nio.ByteBuffer;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.Base64;
import java.util.HashMap;
import java.util.Locale;
import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

// Platform socket/TLS only. No Activity, third-party networking library, or UI state.
public final class WebSocketTransport {
    private final Object sendLock = new Object();
    private final SecureRandom random = new SecureRandom();
    private volatile Socket socket;
    private volatile boolean stopped;
    private InputStream input;
    private OutputStream output;
    private int limit;

    public void open(String address, int timeout, int maximum) throws Exception {
        URI uri = new URI(address);
        boolean secure = "wss".equals(uri.getScheme());
        String host = uri.getHost();
        if (host == null || uri.getUserInfo() != null || uri.getFragment() != null
                || (!secure && !"ws".equals(uri.getScheme())))
            throw new IOException("Invalid WebSocket URL");
        if (!secure && !NetworkSecurityPolicy.getInstance().isCleartextTrafficPermitted(host))
            throw new IOException("Cleartext WebSocket is denied by the application policy");
        int port = uri.getPort() < 0 ? (secure ? 443 : 80) : uri.getPort();
        Socket raw = new Socket();
        synchronized (this) {
            if (stopped) { raw.close(); throw new IOException("WebSocket stopped"); }
            socket = raw;
        }
        raw.connect(new InetSocketAddress(host, port), timeout);
        raw.setSoTimeout(timeout);
        if (secure) {
            SSLSocket tls = (SSLSocket) ((SSLSocketFactory) SSLSocketFactory.getDefault())
                    .createSocket(raw, host, port, true);
            SSLParameters parameters = tls.getSSLParameters();
            parameters.setEndpointIdentificationAlgorithm("HTTPS");
            tls.setSSLParameters(parameters);
            synchronized (this) {
                if (stopped) { tls.close(); throw new IOException("WebSocket stopped"); }
                socket = tls;
            }
            tls.startHandshake();
        }
        input = socket.getInputStream();
        output = socket.getOutputStream();
        limit = maximum;
        byte[] nonce = new byte[16];
        random.nextBytes(nonce);
        String key = Base64.getEncoder().encodeToString(nonce);
        String path = uri.getRawPath();
        if (path == null || path.isEmpty()) path = "/";
        if (uri.getRawQuery() != null) path += "?" + uri.getRawQuery();
        String authority = uri.getRawAuthority();
        String request = "GET " + path + " HTTP/1.1\r\nHost: " + authority
                + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13"
                + "\r\nSec-WebSocket-Key: " + key + "\r\n\r\n";
        for (int i = 0; i < request.length(); ++i)
            if (request.charAt(i) > 127) throw new IOException("WebSocket URL must be ASCII-escaped");
        output.write(request.getBytes(StandardCharsets.US_ASCII));
        output.flush();
        ByteArrayOutputStream header = new ByteArrayOutputStream();
        int tail = 0;
        while (tail != 0x0d0a0d0a) {
            int value = input.read();
            if (value < 0) throw new EOFException();
            if (header.size() >= 16384) throw new IOException("WebSocket upgrade headers are too large");
            header.write(value);
            tail = (tail << 8) | value;
        }
        String[] lines = header.toString("US-ASCII").split("\r\n");
        if (!lines[0].startsWith("HTTP/1.1 101 ") && !lines[0].equals("HTTP/1.1 101"))
            throw new IOException("WebSocket upgrade rejected");
        HashMap<String, String> headers = new HashMap<>();
        for (int i = 1; i < lines.length; ++i) {
            int colon = lines[i].indexOf(':');
            if (colon <= 0) throw new IOException("Invalid upgrade header");
            String name = lines[i].substring(0, colon).toLowerCase(Locale.ROOT);
            String value = lines[i].substring(colon + 1).trim();
            headers.merge(name, value, (a, b) -> a + "," + b);
        }
        String expected = Base64.getEncoder().encodeToString(MessageDigest.getInstance("SHA-1")
                .digest((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").getBytes(StandardCharsets.US_ASCII)));
        if (!expected.equals(headers.get("sec-websocket-accept"))
                || !token(headers.get("upgrade"), "websocket") || !token(headers.get("connection"), "upgrade")
                || headers.containsKey("sec-websocket-extensions") || headers.containsKey("sec-websocket-protocol"))
            throw new IOException("Invalid WebSocket upgrade response");
        socket.setSoTimeout(0);
    }

    private static boolean token(String value, String wanted) {
        if (value == null) return false;
        for (String part : value.split(",")) if (wanted.equalsIgnoreCase(part.trim())) return true;
        return false;
    }

    private int octet() throws IOException {
        int value = input.read();
        if (value < 0) throw new EOFException();
        return value;
    }

    private byte[] read(int size) throws IOException {
        byte[] bytes = new byte[size];
        for (int at = 0; at < size;) {
            int got = input.read(bytes, at, size - at);
            if (got < 0) throw new EOFException();
            at += got;
        }
        return bytes;
    }

    // Leading byte is the completed message opcode (1/2/8). Control frames never escape this class.
    public byte[] receive() throws Exception {
        ByteArrayOutputStream message = new ByteArrayOutputStream();
        int kind = 0;
        for (;;) {
            int first = octet(), second = octet();
            int opcode = first & 15;
            boolean fin = (first & 128) != 0;
            if ((first & 112) != 0 || (second & 128) != 0) throw new IOException("Invalid WebSocket frame flags");
            long length = second & 127;
            if (length == 126) {
                length = (octet() << 8) | octet();
                if (length < 126) throw new IOException("Noncanonical frame length");
            } else if (length == 127) {
                length = 0;
                for (int i = 0; i < 8; ++i) {
                    int value = octet();
                    if (i == 0 && value >= 128) throw new IOException("Invalid frame length");
                    length = (length << 8) | value;
                }
                if (length < 65536) throw new IOException("Noncanonical frame length");
            }
            boolean control = opcode >= 8;
            if (control && (!fin || length > 125)) throw new IOException("Invalid control frame");
            if (!control && length > limit - message.size()) throw new IOException("WebSocket message exceeds its limit");
            byte[] payload = read((int) length);
            if (opcode == 9) { frame(payload, 10); continue; }
            if (opcode == 10) continue;
            if (opcode == 8) {
                if (payload.length == 1) throw new IOException("Invalid close payload");
                if (payload.length >= 2) {
                    int code = ((payload[0] & 255) << 8) | (payload[1] & 255);
                    if (!((code >= 1000 && code <= 1014 && code != 1004 && code != 1005 && code != 1006)
                            || (code >= 3000 && code <= 4999))) throw new IOException("Invalid close code");
                    StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                            .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(payload, 2, payload.length - 2));
                }
                byte[] result = new byte[payload.length + 1];
                result[0] = 8;
                System.arraycopy(payload, 0, result, 1, payload.length);
                return result;
            }
            if (opcode == 1 || opcode == 2) {
                if (kind != 0) throw new IOException("Unexpected new message during fragmentation");
                kind = opcode;
            } else if (opcode != 0 || kind == 0) throw new IOException("Invalid continuation frame");
            message.write(payload);
            if (fin) {
                byte[] bytes = message.toByteArray();
                byte[] result = new byte[bytes.length + 1];
                result[0] = (byte) kind;
                System.arraycopy(bytes, 0, result, 1, bytes.length);
                return result;
            }
        }
    }

    private void frame(byte[] bytes, int opcode) throws IOException {
        synchronized (sendLock) {
            byte[] mask = new byte[4];
            random.nextBytes(mask);
            output.write(128 | opcode);
            int length = bytes.length;
            if (length < 126) output.write(128 | length);
            else if (length <= 65535) {
                output.write(128 | 126); output.write(length >>> 8); output.write(length & 255);
            } else {
                output.write(128 | 127);
                for (int shift = 56; shift >= 0; shift -= 8) output.write((int) (((long) length >>> shift) & 255));
            }
            output.write(mask);
            byte[] chunk = new byte[Math.min(16384, length)];
            for (int at = 0; at < length;) {
                int count = Math.min(chunk.length, length - at);
                for (int i = 0; i < count; ++i) chunk[i] = (byte) (bytes[at + i] ^ mask[(at + i) & 3]);
                output.write(chunk, 0, count);
                at += count;
            }
            output.flush();
        }
    }

    public void send(byte[] bytes, boolean text) throws IOException { frame(bytes, text ? 1 : 2); }
    public void close(int code, byte[] reason) throws IOException {
        byte[] bytes = new byte[reason.length + 2];
        bytes[0] = (byte) (code >>> 8); bytes[1] = (byte) code;
        System.arraycopy(reason, 0, bytes, 2, reason.length);
        frame(bytes, 8);
    }
    public synchronized void shutdown() {
        stopped = true;
        if (socket != null) try { socket.close(); } catch (IOException ignored) { }
    }
}
