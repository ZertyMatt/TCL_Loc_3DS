// TCL Localisation pour 3DS - étape 1 : trafic, Vélo'v, arrêts + passages
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "cJSON.h"

#define API "https://tcl-localisation.onrender.com"

static PrintConsole top, bot;

// ---------- HTTP ----------
static char *http_get(const char *url) {
    httpcContext ctx;
    if (R_FAILED(httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 1))) return NULL;
    httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);           // évite les soucis de certificats
    httpcAddRequestHeaderField(&ctx, "User-Agent", "tcl3ds/0.1");
    if (R_FAILED(httpcBeginRequest(&ctx))) { httpcCloseContext(&ctx); return NULL; }
    u32 status = 0;
    httpcGetResponseStatusCode(&ctx, &status);
    // redirections
    for (int i = 0; i < 3 && status >= 300 && status < 400; i++) {
        char loc[512];
        if (R_FAILED(httpcGetResponseHeader(&ctx, "Location", loc, sizeof loc))) break;
        httpcCloseContext(&ctx);
        if (R_FAILED(httpcOpenContext(&ctx, HTTPC_METHOD_GET, loc, 1))) return NULL;
        httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
        httpcBeginRequest(&ctx);
        httpcGetResponseStatusCode(&ctx, &status);
    }
    if (status != 200) { httpcCloseContext(&ctx); return NULL; }
    u32 cap = 0x8000, len = 0, got = 0;
    char *buf = malloc(cap);
    if (!buf) { httpcCloseContext(&ctx); return NULL; }
    Result rc;
    do {
        rc = httpcDownloadData(&ctx, (u8 *)buf + len, cap - len - 1, &got);
        len += got;
        if (rc == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); httpcCloseContext(&ctx); return NULL; }
            buf = nb;
        }
    } while (rc == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING);
    httpcCloseContext(&ctx);
    buf[len] = 0;
    return buf;
}

static cJSON *api_values(const char *path) {
    char url[512];
    snprintf(url, sizeof url, "%s%s", API, path);
    char *txt = http_get(url);
    if (!txt) return NULL;
    cJSON *root = cJSON_Parse(txt);
    free(txt);
    return root;
}

static const char *str(cJSON *o, const char *k) {
    cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}
static int num(cJSON *o, const char *k) {
    cJSON *v = cJSON_GetObjectItem(o, k);
    return cJSON_IsNumber(v) ? v->valueint : 0;
}

// ---------- Utilitaires ----------
static void norm(const char *s, char *out, size_t n) {   // minuscules sans accents (UTF-8 simple)
    size_t j = 0;
    for (size_t i = 0; s[i] && j + 1 < n; i++) {
        unsigned char c = s[i];
        if (c == 0xC3 && s[i + 1]) {
            unsigned char d = s[++i];
            const char *m = "aaaaaaaceeeeiiiidnooooo/ouuuuyps";
            out[j++] = (d >= 0x80 && d <= 0xBF) ? m[(d - 0x80) % 32] : '?';
        } else out[j++] = tolower(c);
    }
    out[j] = 0;
}

static void wait_a(void) {
    printf("\n[A] continuer");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & (KEY_A | KEY_B)) break;
        gspWaitForVBlank();
    }
}

static void head(const char *t) {
    consoleSelect(&top); consoleClear();
    printf("\x1b[1;1H\x1b[41;37m %-48s\x1b[0m\n\n", t);
}

// Liste défilante générique : retourne l'index choisi, -1 si retour
static int pick(const char *title, char **items, int n, const char *hint) {
    int sel = 0, scroll = 0, rows = 24;
    bool redraw = true;
    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown() | hidKeysDownRepeat();
        if (k & KEY_B) return -1;
        if (k & KEY_A && n) return sel;
        if (k & KEY_DOWN && sel < n - 1) { sel++; redraw = true; }
        if (k & KEY_UP && sel > 0) { sel--; redraw = true; }
        if (k & KEY_R) { sel = sel + 10 < n ? sel + 10 : n - 1; redraw = true; }
        if (k & KEY_L) { sel = sel - 10 > 0 ? sel - 10 : 0; redraw = true; }
        if (redraw) {
            if (sel < scroll) scroll = sel;
            if (sel >= scroll + rows) scroll = sel - rows + 1;
            head(title);
            for (int i = scroll; i < n && i < scroll + rows; i++)
                printf("%s%-.48s\x1b[0m\n", i == sel ? "\x1b[47;30m" : "", items[i]);
            if (!n) printf("(aucun resultat)\n");
            consoleSelect(&bot); consoleClear();
            printf("\x1b[1;1H%s\n\n%d/%d", hint, n ? sel + 1 : 0, n);
            redraw = false;
        }
        gspWaitForVBlank();
    }
    return -1;
}

// ---------- Trafic ----------
static void screen_traffic(void) {
    head("Infos trafic"); printf("Chargement...");
    cJSON *root = api_values("/api/traffic");
    cJSON *vals = root ? cJSON_GetObjectItem(root, "values") : NULL;
    int n = cJSON_GetArraySize(vals);
    if (!root || n <= 0) { head("Infos trafic"); printf(root ? "Aucune perturbation." : "Erreur reseau."); wait_a(); cJSON_Delete(root); return; }
    char **items = calloc(n, sizeof *items);
    for (int i = 0; i < n; i++) {
        cJSON *a = cJSON_GetArrayItem(vals, i);
        char b[160];
        snprintf(b, sizeof b, "[%s] %s", str(a, "ligne_com")[0] ? str(a, "ligne_com") : str(a, "ligne_cli"), str(a, "titre"));
        items[i] = strdup(b);
    }
    int s;
    while ((s = pick("Infos trafic", items, n, "[A] detail  [B] retour")) >= 0) {
        cJSON *a = cJSON_GetArrayItem(vals, s);
        consoleSelect(&bot); consoleClear();
        printf("\x1b[1;1H%s\n%s - %s\n\n%.1200s", str(a, "titre"), str(a, "type"), str(a, "cause"), str(a, "message"));
        while (aptMainLoop()) { hidScanInput(); if (hidKeysDown() & (KEY_A | KEY_B)) break; gspWaitForVBlank(); }
    }
    for (int i = 0; i < n; i++) free(items[i]);
    free(items); cJSON_Delete(root);
}

// ---------- Velo'v ----------
static void screen_velov(void) {
    head("Velo'v"); printf("Chargement...");
    cJSON *root = api_values("/api/velov");
    cJSON *vals = root ? cJSON_GetObjectItem(root, "values") : NULL;
    int n = cJSON_GetArraySize(vals);
    if (!root || n <= 0) { head("Velo'v"); printf("Erreur reseau."); wait_a(); cJSON_Delete(root); return; }
    static char kbuf[64] = "";
    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, 40);
    swkbdSetHintText(&kb, "Nom de station (vide = tout)");
    swkbdInputText(&kb, kbuf, sizeof kbuf);
    char q[64]; norm(kbuf, q, sizeof q);
    char **items = calloc(n, sizeof *items); int *idx = calloc(n, sizeof *idx); int m = 0;
    for (int i = 0; i < n; i++) {
        cJSON *s = cJSON_GetArrayItem(vals, i);
        char nm[128]; norm(str(s, "name"), nm, sizeof nm);
        if (q[0] && !strstr(nm, q)) continue;
        cJSON *av = cJSON_GetObjectItem(s, "availabilities");
        if (!av) { cJSON *t = cJSON_GetObjectItem(s, "totalStands"); av = t ? cJSON_GetObjectItem(t, "availabilities") : NULL; }
        int bikes = av ? num(av, "bikes") : num(s, "available_bikes");
        int stands = av ? num(av, "stands") : num(s, "available_bike_stands");
        char b[160]; snprintf(b, sizeof b, "%-30.30s v:%2d p:%2d", str(s, "name"), bikes, stands);
        items[m] = strdup(b); idx[m++] = i;
    }
    pick("Velo'v  (v=velos p=places)", items, m, "[B] retour  L/R = +/-10");
    for (int i = 0; i < m; i++) free(items[i]);
    free(items); free(idx); cJSON_Delete(root);
}

// ---------- Arrêts ----------
static void show_passages(const char *id, const char *nom) {
    head(nom); printf("Chargement des passages...");
    char path[128]; snprintf(path, sizeof path, "/api/stop-passages/%s", id);
    cJSON *root = api_values(path);
    cJSON *vals = root ? cJSON_GetObjectItem(root, "values") : NULL;
    int n = cJSON_GetArraySize(vals);
    head(nom);
    if (!root) printf("Erreur reseau.");
    else if (n <= 0) printf("Aucun passage prevu.");
    else for (int i = 0; i < n && i < 22; i++) {
        cJSON *v = cJSON_GetArrayItem(vals, i);
        printf("%s%s %-5.5s > %-.30s\x1b[0m\n", cJSON_IsTrue(cJSON_GetObjectItem(v, "e")) || num(v, "e") ? "\x1b[32m" : "", str(v, "t"), str(v, "l"), str(v, "h"));
    }
    printf("\n(vert = temps reel)");
    wait_a();
    cJSON_Delete(root);
}

static void screen_stops(void) {
    head("Arrets"); printf("Chargement des arrets (patience)...");
    cJSON *root = api_values("/api/stops");
    cJSON *vals = root ? cJSON_GetObjectItem(root, "values") : NULL;
    int n = cJSON_GetArraySize(vals);
    if (!root || n <= 0) { head("Arrets"); printf("Erreur reseau ou memoire."); wait_a(); cJSON_Delete(root); return; }
    while (aptMainLoop()) {
        static char kbuf[64] = "";
        SwkbdState kb;
        swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, 40);
        swkbdSetHintText(&kb, "Nom d'arret ou commune");
        if (swkbdInputText(&kb, kbuf, sizeof kbuf) != SWKBD_BUTTON_CONFIRM || !kbuf[0]) break;
        char q[64]; norm(kbuf, q, sizeof q);
        char **items = calloc(n, sizeof *items); int *idx = calloc(n, sizeof *idx); int m = 0;
        for (int i = 0; i < n && m < 400; i++) {
            cJSON *s = cJSON_GetArrayItem(vals, i);
            char nm[200]; snprintf(nm, sizeof nm, "%s %s", str(s, "nom"), str(s, "commune"));
            char nn[200]; norm(nm, nn, sizeof nn);
            if (!strstr(nn, q)) continue;
            char b[160]; snprintf(b, sizeof b, "%-28.28s %-16.16s", str(s, "nom"), str(s, "commune"));
            items[m] = strdup(b); idx[m++] = i;
        }
        int s;
        while ((s = pick("Arrets", items, m, "[A] passages  [B] nouvelle recherche")) >= 0) {
            cJSON *st = cJSON_GetArrayItem(vals, idx[s]);
            cJSON *idv = cJSON_GetObjectItem(st, "id");
            char id[32];
            if (cJSON_IsNumber(idv)) snprintf(id, sizeof id, "%d", idv->valueint); else snprintf(id, sizeof id, "%s", str(st, "id"));
            show_passages(id, str(st, "nom"));
        }
        for (int i = 0; i < m; i++) free(items[i]);
        free(items); free(idx);
    }
    cJSON_Delete(root);
}

// ---------- Menu ----------
int main(void) {
    gfxInitDefault();
    consoleInit(GFX_TOP, &top);
    consoleInit(GFX_BOTTOM, &bot);
    httpcInit(0x100000);

    char *menu[] = { "Infos trafic", "Arrets et prochains passages", "Velo'v" };
    int c;
    while (aptMainLoop() && (c = pick("TCL Localisation (3DS)", menu, 3, "[A] ouvrir   [START] quitter")) != -2) {
        if (c == 0) screen_traffic();
        else if (c == 1) screen_stops();
        else if (c == 2) screen_velov();
        hidScanInput();
        if (hidKeysHeld() & KEY_START) break;
    }
    httpcExit();
    gfxExit();
    return 0;
}
