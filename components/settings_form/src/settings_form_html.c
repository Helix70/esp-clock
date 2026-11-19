#include "settings_form.h"

#include "esp_app_desc.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Page is emitted in pieces: static markup from the strings below, with
 * field values (escaped) and the timezone options built at runtime. */
static const char HTML_HEAD[] =
"<!DOCTYPE html>"
"<html><head>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ESP Clock</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#e0e0e0;"
"min-height:100vh;display:flex;align-items:center;justify-content:center;"
"padding:24px 0}"
".card{background:#16213e;border-radius:12px;padding:32px;width:90%;"
"max-width:420px;box-shadow:0 8px 32px rgba(0,0,0,.4)}"
"h1{text-align:center;color:#0ff;margin-bottom:24px;font-size:1.5em}"
"label{display:block;margin-top:16px;font-size:.9em;color:#aaa}"
"input[type=text],input[type=password],input[type=number],select{"
"width:100%;padding:10px;margin-top:4px;background:#0f3460;"
"border:1px solid #333;border-radius:6px;color:#fff;font-size:1em}"
"input[type=range]{width:100%;margin-top:8px}"
"input[type=color]{width:56px;height:36px;margin-top:4px;border:none;"
"background:none}"
"input[type=file]{width:100%;margin-top:12px}"
"progress{width:100%;margin-top:12px}"
".row{display:flex;gap:12px;margin-top:4px}"
".chk{display:flex;align-items:center;gap:8px;margin-top:12px}"
".chk input{width:18px;height:18px}"
".hint{font-size:.8em;color:#777;margin-top:4px}"
".st{text-align:center;margin-top:12px;min-height:1.2em}"
".section{margin-top:24px;padding-top:16px;border-top:1px solid #333}"
".section h2{font-size:1.1em;color:#0af;margin-bottom:8px}"
"button{width:100%;padding:14px;margin-top:28px;background:#0af;"
"color:#000;border:none;border-radius:6px;font-size:1.1em;"
"font-weight:bold;cursor:pointer}"
"button:hover{background:#0cf}"
"button.sm{width:auto;margin:4px 0 0 8px;padding:10px 14px;font-size:.9em}"
"</style></head><body>"
"<div class='card'>";

static const char HTML_WIFI[] =
"<div class='section'><h2>WiFi</h2>"
"<label>Nearby networks"
"<div style='display:flex'>"
"<select id='nets' onchange=\"if(this.value)document.forms[0].ssid.value=this.value\">"
"<option value=''>Scanning...</option></select>"
"<button type='button' class='sm' onclick='scan(1)'>Rescan</button>"
"</div></label>";

/* The firmware upload sits outside the settings form: its fields are not
 * settings, and the file must go up as a raw body, not a form. */
static const char HTML_UPLOAD[] =
"<input type='password' id='pw' placeholder='OTA password'>"
"<input type='file' id='fw' accept='.bin'>"
"<button type='button' onclick='up()'>Upload Firmware</button>"
"<progress id='pg' value='0' max='100'></progress>"
"<p class='st' id='ust'></p>";

/* Show only the inputs the chosen colour mode uses. */
static const char SCRIPT_COMMON[] =
"<script>"
"function cm(){"
"var m=+document.forms[0].color_mode.value,"
"n=[1,2,4,0,0][m];"
"for(var i=0;i<4;i++)document.getElementById('c'+i).style.display=i<n?'':'none';"
"document.getElementById('spd').style.display=m>=3?'':'none';}"
"function ab(){"
"var a=document.forms[0].auto_br.checked;"
"document.getElementById('man').style.display=a?'none':'';"
"document.getElementById('aut').style.display=a?'':'none';}"
"cm();ab();";

static const char SCRIPT_SETUP[] =
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

/* Save in place, so the page stays put and says how it went. */
static const char SCRIPT_NETWORK[] =
"function sv(f){"
"var st=document.getElementById('sst');st.textContent='Saving...';"
"fetch('/save',{method:'POST',body:new URLSearchParams(new FormData(f))})"
".then(function(r){return r.text()}).then(function(t){st.textContent=t;})"
".catch(function(){st.textContent='Save failed';});return false;}"
"function up(){"
"var f=document.getElementById('fw').files[0],st=document.getElementById('ust');"
"if(!f){st.textContent='Choose a .bin file first';return;}"
"var x=new XMLHttpRequest();x.open('POST','/update');"
"x.setRequestHeader('X-OTA-Password',document.getElementById('pw').value);"
"x.upload.onprogress=function(e){if(e.lengthComputable)"
"document.getElementById('pg').value=100*e.loaded/e.total;};"
"x.onload=function(){st.textContent=x.responseText;};"
"x.onerror=function(){st.textContent='Upload failed';};"
"st.textContent='Uploading...';x.send(f);}"
"</script></body></html>";

static const char *const COLON_LABELS[] = {"Steady", "Blink", "Blink fast"};
static const char *const COLOR_MODE_LABELS[COLOR_MODE_COUNT] = {
    "Solid", "Gradient (left to right)", "Per digit", "Rainbow cycle",
    "Rainbow wave",
};
static const char *const COLOR_LABELS[CLOCK_SETTINGS_NUM_COLORS] = {
    "Colour 1", "Colour 2", "Colour 3", "Colour 4",
};

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

static void append_str(char *buf, size_t size, size_t *pos, const char *s)
{
    append_fmt(buf, size, pos, "%s", s);
}

/* <label>Label<input type=... name=... value='escaped'></label> */
static void text_input(char *buf, size_t size, size_t *pos, const char *label,
                       const char *type, const char *name, const char *value,
                       int maxlen)
{
    append_fmt(buf, size, pos, "<label>%s<input type='%s' name='%s' value='",
               label, type, name);
    append_escaped(buf, size, pos, value);
    append_fmt(buf, size, pos, "' maxlength='%d'></label>", maxlen);
}

static void checkbox(char *buf, size_t size, size_t *pos, const char *name,
                     bool checked, const char *label, const char *onchange)
{
    append_fmt(buf, size, pos,
               "<div class='chk'><input type='checkbox' name='%s' value='1'%s%s%s%s>"
               "<span>%s</span></div>",
               name, checked ? " checked" : "",
               onchange ? " onchange='" : "", onchange ? onchange : "",
               onchange ? "'" : "", label);
}

static void select_input(char *buf, size_t size, size_t *pos, const char *label,
                         const char *name, const char *const *options,
                         int count, int selected, const char *onchange)
{
    append_fmt(buf, size, pos, "<label>%s<select name='%s'%s%s%s>", label, name,
               onchange ? " onchange='" : "", onchange ? onchange : "",
               onchange ? "'" : "");
    for (int i = 0; i < count; i++) {
        append_fmt(buf, size, pos, "<option value='%d'%s>%s</option>", i,
                   i == selected ? " selected" : "", options[i]);
    }
    append_str(buf, size, pos, "</select></label>");
}

static void range_input(char *buf, size_t size, size_t *pos, const char *label,
                        const char *name, int value, int min, int max)
{
    append_fmt(buf, size, pos,
               "<label>%s <output>%d</output>"
               "<input type='range' name='%s' value='%d' min='%d' max='%d' "
               "oninput='this.previousSibling.value=this.value'></label>",
               label, value, name, value, min, max);
}

char *settings_form_page(const clock_settings_t *s, settings_form_mode_t mode,
                         bool ota_enabled)
{
    bool setup = mode == SETTINGS_FORM_SETUP;
    size_t tz_count;
    const settings_form_tz_t *tz = settings_form_tz_table(&tz_count);

    /* ~100 bytes per timezone option plus room for the form fields. */
    size_t size = sizeof(HTML_HEAD) + sizeof(HTML_WIFI) + sizeof(HTML_UPLOAD) +
                  sizeof(SCRIPT_COMMON) + sizeof(SCRIPT_SETUP) +
                  sizeof(SCRIPT_NETWORK) + tz_count * 100 + 6144;
    char *buf = malloc(size);
    if (!buf) return NULL;
    size_t pos = 0;
    buf[0] = '\0';

    append_str(buf, size, &pos, HTML_HEAD);
    append_str(buf, size, &pos, setup ? "<h1>ESP Clock Setup</h1>"
                                      : "<h1>ESP Clock</h1>");
    append_str(buf, size, &pos,
               setup ? "<form method='POST' action='/save'>"
                     : "<form method='POST' action='/save' onsubmit='return sv(this)'>");

    if (setup) {
        append_str(buf, size, &pos, HTML_WIFI);
        text_input(buf, size, &pos, "SSID", "text", "ssid", s->wifi_ssid,
                   CLOCK_SETTINGS_SSID_MAX);
        text_input(buf, size, &pos, "Password", "password", "pass", s->wifi_pass,
                   CLOCK_SETTINGS_PASS_MAX);
        append_str(buf, size, &pos, "</div>");
    }

    /* Time */
    append_str(buf, size, &pos, "<div class='section'><h2>Time</h2>");
    text_input(buf, size, &pos, "NTP Server", "text", "ntp", s->ntp_server,
               CLOCK_SETTINGS_NTP_MAX);
    append_str(buf, size, &pos, "<label>Timezone<select name='tz'>");
    bool matched = false;
    for (size_t i = 0; i < tz_count; i++) {
        bool sel = strcmp(tz[i].posix, s->tz) == 0;
        if (sel) matched = true;
        append_str(buf, size, &pos, "<option value='");
        append_escaped(buf, size, &pos, tz[i].posix);
        append_fmt(buf, size, &pos, "'%s>", sel ? " selected" : "");
        append_escaped(buf, size, &pos, tz[i].label);
        append_str(buf, size, &pos, "</option>");
    }
    if (!matched) {
        /* Keep a hand-set POSIX string from being silently replaced. */
        append_str(buf, size, &pos, "<option value='");
        append_escaped(buf, size, &pos, s->tz);
        append_str(buf, size, &pos, "' selected>Custom: ");
        append_escaped(buf, size, &pos, s->tz);
        append_str(buf, size, &pos, "</option>");
    }
    append_str(buf, size, &pos, "</select></label>");
    checkbox(buf, size, &pos, "time_24h", s->time_24h, "24-hour format", NULL);
    select_input(buf, size, &pos, "Colon", "colon", COLON_LABELS, 3,
                 s->colon_mode, NULL);
    append_str(buf, size, &pos,
               "<p class='hint'>The upper colon dot goes out while WiFi is down.</p>"
               "</div>");

    /* Brightness */
    append_str(buf, size, &pos, "<div class='section'><h2>Brightness</h2>");
    checkbox(buf, size, &pos, "auto_br", s->auto_brightness,
             "Automatic (light sensor)", "ab()");
    append_str(buf, size, &pos, "<div id='man'>");
    range_input(buf, size, &pos, "Brightness", "brightness", s->brightness, 1,
                CLOCK_BRIGHTNESS_MAX);
    append_str(buf, size, &pos, "</div><div id='aut'>");
    range_input(buf, size, &pos, "Minimum (dark room)", "br_min", s->bright_min,
                1, CLOCK_BRIGHTNESS_MAX);
    range_input(buf, size, &pos, "Maximum (bright room)", "br_max", s->bright_max,
                1, CLOCK_BRIGHTNESS_MAX);
    append_str(buf, size, &pos, "</div></div>");

    /* Colour */
    append_str(buf, size, &pos, "<div class='section'><h2>Colour</h2>");
    select_input(buf, size, &pos, "Mode", "color_mode", COLOR_MODE_LABELS,
                 COLOR_MODE_COUNT, s->color_mode, "cm()");
    append_str(buf, size, &pos, "<div class='row'>");
    for (int i = 0; i < CLOCK_SETTINGS_NUM_COLORS; i++) {
        append_fmt(buf, size, &pos,
                   "<label id='c%d'>%s<br><input type='color' name='color%d' "
                   "value='#%06lx'></label>",
                   i, COLOR_LABELS[i], i, (unsigned long)s->colors[i]);
    }
    append_str(buf, size, &pos, "</div><div id='spd'>");
    append_fmt(buf, size, &pos,
               "<label>Seconds per colour cycle<input type='number' "
               "name='fx_speed' value='%u' min='1' max='600'></label>",
               s->effect_speed);
    append_str(buf, size, &pos, "</div></div>");

    /* Date and temperature cycling */
    append_str(buf, size, &pos, "<div class='section'><h2>Date &amp; Temperature</h2>");
    checkbox(buf, size, &pos, "cyc_date", s->cycle_date,
             "Show the date between the time", NULL);
    checkbox(buf, size, &pos, "cyc_temp", s->cycle_temp,
             "Show the temperature between the time", NULL);
    append_fmt(buf, size, &pos,
               "<label>Seconds of time between them<input type='number' "
               "name='cyc_int' value='%u' min='5' max='240'></label>",
               s->cycle_interval);
    static const char *const DATE_FORMATS[] = {"DD-MM", "MM-DD"};
    select_input(buf, size, &pos, "Date format", "date_mf", DATE_FORMATS, 2,
                 s->date_month_first ? 1 : 0, NULL);
    checkbox(buf, size, &pos, "fahrenheit", s->fahrenheit, "Fahrenheit", NULL);
    int off = s->temp_offset_tenths;
    append_fmt(buf, size, &pos,
               "<label>Calibration offset (&deg;C)"
               "<input type='number' name='temp_offset' value='%s%d.%d' "
               "min='-%d' max='%d' step='0.1'></label>"
               "<p class='hint'>Added to the sensor reading. The clock warms "
               "its own thermistor, so it usually needs a negative offset: "
               "set it to the real temperature minus what the clock shows.</p>"
               "</div>",
               off < 0 ? "-" : "", abs(off) / 10, abs(off) % 10,
               CLOCK_TEMP_OFFSET_MAX / 10, CLOCK_TEMP_OFFSET_MAX / 10);

    /* Buzzer */
    append_str(buf, size, &pos, "<div class='section'><h2>Sound</h2>");
    checkbox(buf, size, &pos, "key_beep", s->key_beep, "Beep on button presses", NULL);
    checkbox(buf, size, &pos, "chime", s->hourly_chime, "Chime on the hour", NULL);
    append_str(buf, size, &pos, "</div>");

    if (setup) {
        /* Firmware updates */
        append_str(buf, size, &pos, "<div class='section'><h2>Firmware Updates</h2>");
        text_input(buf, size, &pos, "OTA password", "password", "ota_pass",
                   s->ota_password, CLOCK_SETTINGS_OTA_PASS_MAX);
        append_str(buf, size, &pos,
                   "<p class='hint'>Leave blank to turn over-the-air updates off. "
                   "When set, open the clock's IP address in a browser to change "
                   "settings and upload new firmware.</p></div>"
                   "<button type='submit'>Save &amp; Restart</button>"
                   "</form></div>");
        append_str(buf, size, &pos, SCRIPT_COMMON);
        append_str(buf, size, &pos, SCRIPT_SETUP);
        return buf;
    }

    append_str(buf, size, &pos,
               "<button type='submit'>Save</button>"
               "<p class='st' id='sst'></p></form>");

    append_str(buf, size, &pos, "<div class='section'><h2>Firmware Update</h2>"
                                "<p class='hint'>Running ");
    append_escaped(buf, size, &pos, esp_app_get_description()->version);
    append_str(buf, size, &pos, "</p>");
    if (ota_enabled)
        append_str(buf, size, &pos, HTML_UPLOAD);
    else
        append_str(buf, size, &pos,
                   "<p class='hint'>Over-the-air updates are off. To turn them "
                   "on, hold button 1 for 2 seconds to open setup and set an "
                   "OTA password.</p>");
    append_str(buf, size, &pos,
               "<p class='hint'>WiFi and the OTA password are only changed in "
               "setup.</p></div></div>");
    append_str(buf, size, &pos, SCRIPT_COMMON);
    append_str(buf, size, &pos, SCRIPT_NETWORK);
    return buf;
}
