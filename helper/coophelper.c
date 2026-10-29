/*
 * coophelper: UDP bridge between the game's Lua probe and a remote peer.
 *
 *   coophelper.exe <dir> <id> <localPort> <peerHost> <peerPort>
 *
 * Lua overwrites   <dir>\out_<id>.txt   with its latest state line (text, <= 1000 bytes).
 * This helper sends that line as one UDP datagram whenever the file content changes.
 * Datagrams from the peer are written to   <dir>\in_<id>.txt   (last one wins, written via temp + rename).
 * No game memory is touched; this is only transport. UNTESTED outside loopback.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define MAXLINE 1024

static int read_file(const char *path, char *buf, int cap)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, (DWORD)(cap - 1), &got, NULL);
    CloseHandle(h);
    if (!ok) return -1;
    buf[got] = 0;
    return (int)got;
}

static void write_atomic(const char *path, const char *tmp, const char *data, int len)
{
    HANDLE h = CreateFileA(tmp, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(h, data, (DWORD)len, &w, NULL);
    CloseHandle(h);
    MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING);
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: %s <dir> <id> <localPort> <peerHost> <peerPort>\n", argv[0]);
        return 2;
    }
    const char *dir = argv[1], *id = argv[2], *peerHost = argv[4];
    int localPort = atoi(argv[3]), peerPort = atoi(argv[5]);

    char outPath[MAX_PATH], inPath[MAX_PATH], inTmp[MAX_PATH];
    snprintf(outPath, sizeof outPath, "%s\\out_%s.txt", dir, id);
    snprintf(inPath, sizeof inPath, "%s\\in_%s.txt", dir, id);
    snprintf(inTmp, sizeof inTmp, "%s\\in_%s.tmp", dir, id);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return 1;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in local = {0}, peer = {0};
    local.sin_family = AF_INET; local.sin_addr.s_addr = htonl(INADDR_ANY); local.sin_port = htons((u_short)localPort);
    if (bind(s, (struct sockaddr *)&local, sizeof local) != 0) { fprintf(stderr, "bind failed %d\n", WSAGetLastError()); return 1; }
    peer.sin_family = AF_INET; peer.sin_port = htons((u_short)peerPort);
    if (inet_pton(AF_INET, peerHost, &peer.sin_addr) != 1) { fprintf(stderr, "bad peer host\n"); return 1; }
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);

    fprintf(stderr, "[coophelper %s] local :%d -> %s:%d, dir=%s\n", id, localPort, peerHost, peerPort, dir);

    char last[MAXLINE] = "", cur[MAXLINE], rx[MAXLINE];
    unsigned long sent = 0, recvd = 0;
    DWORD lastLog = GetTickCount();
    for (;;) {
        int n = read_file(outPath, cur, sizeof cur);
        if (n > 0 && strcmp(cur, last) != 0) {
            sendto(s, cur, n, 0, (struct sockaddr *)&peer, sizeof peer);
            memcpy(last, cur, (size_t)n + 1);
            sent++;
        }
        for (;;) {
            int r = recvfrom(s, rx, MAXLINE - 1, 0, NULL, NULL);
            if (r <= 0) break;
            rx[r] = 0;
            write_atomic(inPath, inTmp, rx, r);
            recvd++;
        }
        if (GetTickCount() - lastLog > 5000) {
            fprintf(stderr, "[coophelper %s] sent=%lu recv=%lu\n", id, sent, recvd);
            lastLog = GetTickCount();
        }
        Sleep(10);
    }
}
