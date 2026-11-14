#include "clock_settings.h"
#include "portal_internal.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Page is emitted in three parts: head + WiFi section, the timezone
 * <select> options (built at runtime), then the rest of the form. */
static const char HTML_HEAD[] =
"<!DOCTYPE html>"
"<html><head>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ESP Clock Setup</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#e0e0e0;"
"min-height:100vh;display:flex;align-items:center;justify-content:center}"
".card{background:#16213e;border-radius:12px;padding:32px;width:90%%;"
"max-width:420px;box-shadow:0 8px 32px rgba(0,0,0,.4)}"
"h1{text-align:center;color:#0ff;margin-bottom:24px;font-size:1.5em}"
"label{display:block;margin-top:16px;font-size:.9em;color:#aaa}"
"input[type=text],input[type=password],input[type=number],select{"
"width:100%%;padding:10px;margin-top:4px;background:#0f3460;"
"border:1px solid #333;border-radius:6px;color:#fff;font-size:1em}"
".row{display:flex;gap:12px;margin-top:16px}"
".row label{flex:1}"
".row select{flex:1}"
".chk{display:flex;align-items:center;gap:8px;margin-top:12px}"
".chk input{width:18px;height:18px}"
".section{margin-top:24px;padding-top:16px;border-top:1px solid #333}"
".section h2{font-size:1.1em;color:#0af;margin-bottom:8px}"
"button{width:100%%;padding:14px;margin-top:28px;background:#0af;"
"color:#000;border:none;border-radius:6px;font-size:1.1em;"
"font-weight:bold;cursor:pointer}"
"button:hover{background:#0cf}"
"button.sm{width:auto;margin:4px 0 0 8px;padding:10px 14px;font-size:.9em}"
"</style></head><body>"
"<div class='card'>"
"<h1>ESP Clock Setup</h1>"
"<form method='POST' action='/save'>"
"<div class='section'><h2>WiFi</h2>"
"<label>Nearby networks"
"<div style='display:flex'>"
"<select id='nets' onchange=\"if(this.value)document.forms[0].ssid.value=this.value\">"
"<option value=''>Scanning...</option></select>"
"<button type='button' class='sm' onclick='scan(1)'>Rescan</button>"
"</div></label>"
"<label>SSID<input type='text' name='ssid' value='%s' maxlength='32'></label>"
"<label>Password<input type='password' name='pass' value='%s' maxlength='64'></label>"
"</div>"
"<div class='section'><h2>Time</h2>"
"<label>NTP Server<input type='text' name='ntp' value='%s'></label>"
"<label>Timezone<select name='tz'>";

static const char HTML_TAIL[] =
"</select></label>"
"<div class='chk'><input type='checkbox' name='time_24h' value='1' %s>"
"<span>24-hour format</span></div>"
"<div class='chk'><input type='checkbox' name='blink_sep' value='1' %s>"
"<span>Blink separator</span></div>"
"</div>"
"<div class='section'><h2>Display</h2>"
"<label>Brightness (1-5)<input type='number' name='brightness' value='%d'"
" min='1' max='5'></label>"
"<div style='margin-top:12px'><span style='color:#aaa;font-size:.9em'>"
"LED Color Channels</span></div>"
"<div class='chk'><input type='checkbox' name='led_r' value='1' %s>"
"<span style='color:#f44'>Red</span></div>"
"<div class='chk'><input type='checkbox' name='led_g' value='1' %s>"
"<span style='color:#4f4'>Green</span></div>"
"<div class='chk'><input type='checkbox' name='led_b' value='1' %s>"
"<span style='color:#44f'>Blue</span></div>"
"</div>"
"<div class='section'><h2>Temperature</h2>"
"<div class='chk'><input type='checkbox' name='fahrenheit' value='1' %s>"
"<span>Fahrenheit</span></div>"
"<label>Calibration offset (tenths &deg;)"
"<input type='number' name='temp_offset' value='%d' min='-50' max='50'>"
"</label>"
"</div>"
"<button type='submit'>Save &amp; Restart</button>"
"</form></div>"
"<script>"
"function scan(r){"
"var s=document.getElementById('nets');"
"s.innerHTML='<option value=\"\">Scanning...</option>';"
"fetch('/scan'+(r?'?r=1':'')).then(function(x){return x.json()})"
".then(function(a){"
"var cur=document.forms[0].ssid.value;"
"s.innerHTML='<option value=\"\">'+(a.length?'Select a network':'No networks found')+'</option>';"
"a.forEach(function(n){"
"var o=document.createElement('option');o.value=n.ssid;"
"o.textContent=n.ssid+' ('+n.rssi+' dBm)'+(n.sec?' \\uD83D\\uDD12':'');"
"if(n.ssid===cur)o.selected=true;s.appendChild(o);});"
"}).catch(function(){s.innerHTML='<option value=\"\">Scan failed</option>';});"
"}"
"scan(0);"
"</script></body></html>";

static const char OOM_PAGE[] = "<html><body>Out of memory</body></html>";

/* Append an HTML-attribute-safe copy of src to buf at *pos. */
static void append_escaped(char *buf, size_t size, size_t *pos, const char *src)
{
    for (; *src; src++) {
        const char *rep = NULL;
        switch (*src) {
        case '<': rep = "&lt;"; break;
        case '>': rep = "&gt;"; break;
        case '&': rep = "&amp;"; break;
        case '\'': rep = "&#39;"; break;
        case '"': rep = "&quot;"; break;
        default: break;
        }
        if (rep) {
            size_t rl = strlen(rep);
            if (*pos + rl >= size) return;
            memcpy(buf + *pos, rep, rl);
            *pos += rl;
        } else {
            if (*pos + 1 >= size) return;
            buf[(*pos)++] = *src;
        }
    }
    buf[*pos] = '\0';
}

static void append_fmt(char *buf, size_t size, size_t *pos, const char *fmt, ...)
{
    if (*pos >= size) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *pos, size - *pos, fmt, ap);
    va_end(ap);
    if (n > 0) *pos += ((size_t)n < size - *pos) ? (size_t)n : size - *pos - 1;
}

const char *portal_html_get_page(const clock_settings_t *s)
{
    size_t tz_count;
    const portal_tz_t *tz = portal_tz_table(&tz_count);

    /* ~100 bytes per timezone option plus room for field values. */
    size_t size = sizeof(HTML_HEAD) + sizeof(HTML_TAIL) + tz_count * 100 + 512;
    char *buf = malloc(size);
    if (!buf) return OOM_PAGE;

    size_t pos = 0;
    append_fmt(buf, size, &pos, HTML_HEAD, s->wifi_ssid, s->wifi_pass,
               s->ntp_server);

    bool matched = false;
    for (size_t i = 0; i < tz_count; i++) {
        bool sel = strcmp(tz[i].posix, s->tz) == 0;
        if (sel) matched = true;
        append_fmt(buf, size, &pos, "<option value='");
        append_escaped(buf, size, &pos, tz[i].posix);
        append_fmt(buf, size, &pos, "'%s>", sel ? " selected" : "");
        append_escaped(buf, size, &pos, tz[i].label);
        append_fmt(buf, size, &pos, "</option>");
    }
    if (!matched) {
        /* Keep a hand-set POSIX string from being silently replaced. */
        append_fmt(buf, size, &pos, "<option value='");
        append_escaped(buf, size, &pos, s->tz);
        append_fmt(buf, size, &pos, "' selected>Custom: ");
        append_escaped(buf, size, &pos, s->tz);
        append_fmt(buf, size, &pos, "</option>");
    }

    append_fmt(buf, size, &pos, HTML_TAIL,
        s->time_24h ? "checked" : "",
        s->blink_separator ? "checked" : "",
        s->brightness,
        s->led_r ? "checked" : "",
        s->led_g ? "checked" : "",
        s->led_b ? "checked" : "",
        s->fahrenheit ? "checked" : "",
        s->temp_offset_tenths);

    return buf;
}

void portal_html_free_page(const char *page)
{
    if (page != OOM_PAGE) free((void *)page);
}
