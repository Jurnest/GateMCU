#include <stdio.h>
#include <string.h>
#include "gateway.h"
#include "web_internal.h"
#include "esp_http_server.h"

static const char appliance_styles[] =
    "<style>\n"
    ":root{color-scheme:dark;--page:#000000;--surface:#0A0A0A;--raised:#111111;--border:#262626;--text:#F5F5F5;--secondary:#8A8A8A;--muted:#606060;--accent:#3B82F6;--success:#22C55E;--warning:#F59E0B;--danger:#EF4444;--radius:3px}\n"
    "*{box-sizing:border-box}\n"
    "html{min-width:320px;background:var(--page)}\n"
    "body{margin:0;background:var(--page);color:var(--text);font:14px/1.45 system-ui,-apple-system,'Segoe UI',sans-serif}\n"
    "button,input,select,textarea{font:inherit}\n"
    "a{color:inherit}\n"
    "button,select{cursor:pointer}\n"
    "button:focus-visible,a:focus-visible,input:focus-visible,select:focus-visible,textarea:focus-visible{outline:2px solid var(--accent);outline-offset:2px}\n"
    ".layout{min-height:100vh;display:grid;grid-template-columns:218px minmax(0,1fr);background:var(--page)}\n"
    ".sidebar{position:sticky;top:0;height:100vh;display:flex;flex-direction:column;border-right:1px solid var(--border);background:#000;min-width:0}\n"
    ".brand{height:62px;display:flex;align-items:center;gap:10px;padding:0 18px;border-bottom:1px solid var(--border);font-size:15px;font-weight:650;letter-spacing:-.02em}\n"
    ".brand-mark{width:25px;height:25px;display:grid;place-items:center;border:1px solid var(--accent);color:var(--accent);font-size:13px;font-weight:700;border-radius:2px}\n"
    ".nav-label{padding:21px 18px 7px;color:var(--muted);font-size:11px}\n"
    ".nav-list{display:grid;gap:2px;padding:0 9px}\n"
    ".nav-link{min-height:36px;display:flex;align-items:center;gap:10px;padding:7px 9px;color:var(--secondary);text-decoration:none;border:1px solid transparent;border-radius:var(--radius)}\n"
    ".nav-link:hover{color:var(--text);background:var(--surface)}\n"
    ".nav-link.active{color:var(--text);background:var(--raised);border-color:var(--border)}\n"
    ".nav-icon{width:17px;text-align:center;color:var(--muted);font-size:14px;flex:0 0 auto}\n"
    ".nav-link.active .nav-icon{color:var(--accent)}\n"
    ".sidebar-foot{margin-top:auto;padding:13px 18px;border-top:1px solid var(--border);color:var(--muted);font-size:11px}\n"
    ".workspace{min-width:0;background:var(--page)}\n"
    ".topbar{height:62px;display:flex;align-items:center;justify-content:space-between;gap:12px;padding:0 26px;border-bottom:1px solid var(--border);background:#000}\n"
    ".crumb{font-size:13px;color:var(--secondary)}\n"
    ".top-actions{display:flex;align-items:center;gap:9px}\n"
    ".lang,.mobile-menu,.button,.input,select.input,textarea.input{min-height:34px;border:1px solid var(--border);border-radius:var(--radius);background:var(--surface);color:var(--text)}\n"
    ".lang,.mobile-menu{padding:6px 9px}\n"
    ".logout{min-height:32px;padding:5px 10px;border:1px solid var(--border);border-radius:var(--radius);background:#000;color:var(--secondary)}\n"
    ".logout:hover{color:var(--text);border-color:var(--secondary)}\n"
    ".mobile-menu{display:none;max-width:48vw}\n"
    ".page{width:min(100%,1120px);margin:0 auto;padding:27px 26px 48px;min-width:0}\n"
    ".page-heading{display:flex;justify-content:space-between;align-items:flex-start;gap:14px;margin-bottom:21px}\n"
    "h1{font-size:21px;line-height:1.25;font-weight:600;letter-spacing:-.025em;margin:0}\n"
    "h2{font-size:14px;font-weight:600;margin:0}\n"
    ".lede{margin:5px 0 0;color:var(--secondary);font-size:12px}\n"
    ".panel{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);margin-bottom:14px;min-width:0}\n"
    ".panel-head{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:12px 14px;border-bottom:1px solid var(--border)}\n"
    ".panel-body{padding:14px}\n"
    ".status{display:inline-flex;align-items:center;gap:7px;color:var(--secondary);font-size:12px;white-space:nowrap}\n"
    ".status::before{content:'';width:7px;height:7px;border-radius:50%;background:var(--muted)}\n"
    ".status.ok::before{background:var(--success)}.status.warn::before{background:var(--warning)}.status.bad::before{background:var(--danger)}\n"
    ".data-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr))}\n"
    ".data-cell{padding:12px 14px;min-width:0;border-right:1px solid var(--border)}\n"
    ".data-cell:last-child{border-right:0}\n"
    ".data-label{display:block;color:var(--muted);font-size:11px;margin-bottom:5px}\n"
    ".data-value{display:block;color:var(--text);font-size:13px;font-weight:550;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}\n"
    ".usage-list{display:grid;gap:14px}\n"
    ".usage-row{min-width:0}\n"
    ".usage-head{display:flex;justify-content:space-between;gap:10px;margin-bottom:6px;font-size:12px}\n"
    ".usage-name{color:var(--secondary)}.usage-value{color:var(--text);font-variant-numeric:tabular-nums;text-align:right;overflow-wrap:anywhere}\n"
    ".track{height:6px;background:var(--raised);border:1px solid var(--border);border-radius:1px;overflow:hidden}\n"
    ".fill{height:100%;width:0;background:var(--accent)}\n"
    ".fill.success{background:var(--success)}\n"
    ".table-wrap{width:100%;min-width:0;overflow-x:auto}\n"
    "table{width:100%;border-collapse:collapse;table-layout:fixed;text-align:left;font-size:12px}\n"
    "th{padding:9px 10px;color:var(--muted);font-weight:500;border-bottom:1px solid var(--border)}\n"
    "td{padding:10px;border-bottom:1px solid var(--border);color:var(--secondary);overflow-wrap:anywhere;font-variant-numeric:tabular-nums}\n"
    "tbody tr:last-child td{border-bottom:0}\n"
    "td strong{color:var(--text);font-weight:550}\n"
    ".empty-row{text-align:center;color:var(--muted);padding:20px 10px}\n"
    ".form-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:0 14px}\n"
    ".field{min-width:0;margin:0 0 13px}\n"
    ".field.full{grid-column:1/-1}\n"
    "label{display:block;margin:0 0 5px;color:var(--secondary);font-size:12px}\n"
    ".input{display:block;width:100%;padding:7px 9px;min-width:0;background:#000;outline:none}\n"
    "textarea.input{min-height:230px;resize:vertical;line-height:1.5;font:12px/1.5 ui-monospace,SFMono-Regular,Consolas,monospace;white-space:pre}\n"
    ".input::placeholder{color:var(--muted)}\n"
    ".input:focus-visible{border-color:var(--accent)}\n"
    ".field-note{margin:5px 0 0;color:var(--muted);font-size:11px}\n"
    ".button{display:inline-flex;align-items:center;justify-content:center;gap:7px;min-height:34px;padding:6px 11px;background:var(--raised);color:var(--text);font-size:12px;font-weight:550;white-space:nowrap}\n"
    ".button:hover{border-color:var(--secondary)}\n"
    ".button.primary{background:var(--accent);border-color:var(--accent);color:var(--text)}\n"
    ".button.primary:hover{background:var(--accent)}\n"
    ".button.danger{color:var(--danger)}\n"
    ".button:disabled{opacity:.55;cursor:wait}\n"
    ".button-row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}\n"
    ".inline-actions{display:flex;align-items:center;gap:7px;flex-wrap:wrap}\n"
    ".notice{padding:9px 11px;border:1px solid var(--border);border-left:2px solid var(--accent);background:#000;color:var(--secondary);font-size:12px;margin:0 0 13px}\n"
    ".notice.warning{border-left-color:var(--warning)}\n"
    ".toast-region{position:fixed;right:16px;bottom:16px;display:grid;gap:7px;z-index:5}\n"
    ".toast{max-width:min(360px,calc(100vw - 32px));padding:10px 12px;border:1px solid var(--border);border-left:2px solid var(--success);background:var(--raised);color:var(--text);font-size:12px}\n"
    ".toast.error{border-left-color:var(--danger)}.toast.warn{border-left-color:var(--warning)}\n"
    ".auth-page{min-height:100vh;display:grid;place-items:center;padding:18px;background:#000}\n"
    ".auth-panel{width:min(100%,390px);background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:22px}\n"
    ".auth-brand{display:flex;align-items:center;gap:10px;margin-bottom:24px;font-size:14px;font-weight:600}\n"
    ".auth-panel h1{font-size:19px;margin-bottom:6px}\n"
    ".auth-copy{margin:0 0 20px;color:var(--secondary);font-size:12px}\n"
    ".auth-error{min-height:18px;margin-top:10px;color:var(--danger);font-size:12px}\n"
    ".setup-top{height:56px;display:flex;align-items:center;justify-content:space-between;padding:0 max(16px,calc((100vw - 720px)/2));border-bottom:1px solid var(--border)}\n"
    ".setup-main{width:min(100%,720px);margin:30px auto;padding:0 16px}\n"
    ".setup-panel{max-width:540px}\n"
    "[hidden]{display:none!important}\n"
    "@media(max-width:760px){.layout{display:block}.sidebar{position:relative;height:auto;border-right:0;border-bottom:1px solid var(--border)}.brand{height:54px;padding:0 14px}.nav-label,.nav-list,.sidebar-foot{display:none}.mobile-menu{display:block;margin-left:auto}.topbar{height:50px;padding:0 14px}.page{padding:20px 14px 36px}.page-heading{margin-bottom:15px}.data-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.data-cell{border-bottom:1px solid var(--border)}.data-cell:nth-child(2){border-right:0}.data-cell:nth-last-child(-n+2){border-bottom:0}.panel-body{padding:12px}.form-grid{grid-template-columns:1fr}.field.full{grid-column:auto}.panel-head{padding:11px 12px}.inline-actions{justify-content:flex-start}}\n"
    "@media(max-width:390px){.topbar{padding:0 10px}.crumb{display:none}.page{padding-right:10px;padding-left:10px}.data-cell{padding:10px}.brand{padding:0 10px}.auth-panel{padding:18px}}\n"
    "</style>\n"
;

esp_err_t web_login_page(httpd_req_t *req)
{
    if (!gateway_auth_configured()) {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/");
        return httpd_resp_send(req, "", 0);
    }
    if (web_session_is_valid(req)) {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/");
        return httpd_resp_send(req, "", 0);
    }
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send_chunk(req, "<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>GateMCU · Sign in</title><link rel='icon' href='data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220 0 32 32%22%3E%3Crect x=%221%22 y=%221%22 width=%2230%22 height=%2230%22 rx=%222%22 fill=%22%23000%22 stroke=%22%233B82F6%22/%3E%3Ctext x=%2216%22 y=%2222%22 text-anchor=%22middle%22 font-size=%2218%22 font-family=%22sans-serif%22 fill=%22%233B82F6%22%3EG%3C/text%3E%3C/svg%3E'>", -1);
    httpd_resp_send_chunk(req, appliance_styles, -1);
    httpd_resp_send_chunk(req, "</head><body><main class='auth-page'><section class='auth-panel'><div class='auth-brand'><span class='brand-mark' aria-hidden='true'>G</span>GateMCU</div><h1 id='title' data-tr='Tekrar hoş geldin' data-en='Sign in'>Sign in</h1><p class='auth-copy' id='subtitle' data-tr='Yönetim paneline erişmek için parolanı gir.' data-en='Enter your password to manage this gateway.'>Enter your password to manage this gateway.</p><div class='field'><label for='lang' data-tr='Dil' data-en='Language'>Language</label><select class='input' id='lang' aria-label='Language' onchange='setLanguage(this.value)'><option value='en'>English</option><option value='tr'>Türkçe</option></select></div><form id='login-form'><div class='field'><label id='password-label' for='password' data-tr='Parola' data-en='Password'>Password</label><input class='input' id='password' name='password' type='password' minlength='8' maxlength='63' autocomplete='current-password' required autofocus></div><button class='button primary' id='submit' type='submit' data-tr='Giriş yap' data-en='Sign in'>Sign in</button><div id='error' class='auth-error' role='status' aria-live='polite'></div></form></section></main><script>function setLanguage(l){document.documentElement.lang=l;localStorage.setItem('lang',l);document.querySelectorAll('[data-tr]').forEach(e=>e.textContent=e.dataset[l==='tr'?'tr':'en']);}let l=localStorage.getItem('lang')||'en';document.getElementById('lang').value=l;setLanguage(l);document.getElementById('login-form').addEventListener('submit',async e=>{e.preventDefault();let r=await fetch('/login',{method:'POST',body:new URLSearchParams(new FormData(e.target))});if(r.ok)location.href='/';else document.getElementById('error').textContent=document.documentElement.lang==='en'?'Incorrect password. Try again.':'Parola yanlış. Tekrar dene.';});</script></body></html>", -1);
    return httpd_resp_send_chunk(req, NULL, 0);
}


static void send_html_escaped(httpd_req_t *req, const char *value)
{
    char buffer[96];
    size_t used = 0;
    for (const char *p = value; *p; p++) {
        const char *replacement = *p == '&' ? "&amp;" : *p == '<' ? "&lt;" : *p == '>' ? "&gt;" : NULL;
        if (replacement) {
            size_t n = strlen(replacement);
            if (used + n >= sizeof(buffer)) { httpd_resp_send_chunk(req, buffer, used); used = 0; }
            memcpy(buffer + used, replacement, n); used += n;
        } else {
            if (used + 1 >= sizeof(buffer)) { httpd_resp_send_chunk(req, buffer, used); used = 0; }
            buffer[used++] = *p;
        }
    }
    if (used) httpd_resp_send_chunk(req, buffer, used);
}

static void format_wireguard_config(const gateway_wireguard_config_t *config, char *out, size_t capacity)
{
    size_t used = 0;
    used += snprintf(out + used, capacity - used, "[Interface]\nPrivateKey = %s\nAddress = %s\n", config->private_key, config->tunnel_ip);
    if (config->dns[0] && used < capacity) used += snprintf(out + used, capacity - used, "DNS = %s\n", config->dns);
    if (used < capacity) used += snprintf(out + used, capacity - used, "\n[Peer]\nPublicKey = %s\n", config->server_public_key);
    if (config->preshared_key[0] && used < capacity) used += snprintf(out + used, capacity - used, "PresharedKey = %s\n", config->preshared_key);
    if (config->allowed_ips[0] && used < capacity) used += snprintf(out + used, capacity - used, "AllowedIPs = %s\n", config->allowed_ips);
    if (config->persistent_keepalive && used < capacity) used += snprintf(out + used, capacity - used, "PersistentKeepalive = %u\n", config->persistent_keepalive);
    if (used < capacity) snprintf(out + used, capacity - used, "Endpoint = %s:%u\n", config->endpoint, config->endpoint_port);
}


static void send_nav_link(httpd_req_t *req, const char *href, const char *icon, const char *tr, const char *en, bool active)
{
    httpd_resp_send_chunk(req, "<a class='nav-link ", -1);
    if (active) httpd_resp_send_chunk(req, "active", -1);
    httpd_resp_send_chunk(req, "' href='", -1);
    httpd_resp_send_chunk(req, href, -1);
    if (active) httpd_resp_send_chunk(req, "' aria-current='page'>", -1);
    else httpd_resp_send_chunk(req, "'>", -1);
    httpd_resp_send_chunk(req, "<span class='nav-icon' aria-hidden='true'>", -1);
    httpd_resp_send_chunk(req, icon, -1);
    httpd_resp_send_chunk(req, "</span><span data-tr='", -1);
    httpd_resp_send_chunk(req, tr, -1);
    httpd_resp_send_chunk(req, "' data-en='", -1);
    httpd_resp_send_chunk(req, en, -1);
    httpd_resp_send_chunk(req, "'>", -1);
    httpd_resp_send_chunk(req, en, -1);
    httpd_resp_send_chunk(req, "</span></a>", -1);
}

esp_err_t web_dashboard_page(httpd_req_t *req)
{
    if (!web_require_session(req)) return ESP_OK;
    const char *uri = req->uri;
    const char *screen = strcmp(uri, "/wireguard") == 0 ? "wireguard" :
                         strcmp(uri, "/forwards") == 0 ? "forwards" :
                         strcmp(uri, "/settings") == 0 ? "settings" :
                         strcmp(uri, "/system") == 0 ? "system" : "overview";
    const char *title_en = strcmp(screen, "wireguard") == 0 ? "WireGuard" :
                           strcmp(screen, "forwards") == 0 ? "Port forwarding" :
                           strcmp(screen, "settings") == 0 ? "Wi-Fi & Network" :
                           strcmp(screen, "system") == 0 ? "Device status" : "Overview";
    gateway_wireguard_config_t wg_config = {0};
    char wg_config_text[640] = {0};
    bool wg_configured = strcmp(screen, "wireguard") == 0 && gateway_wireguard_load(&wg_config);
    bool wg_enabled = gateway_wireguard_is_enabled();
    if (wg_configured) format_wireguard_config(&wg_config, wg_config_text, sizeof(wg_config_text));

    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send_chunk(req, "<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>GateMCU</title><link rel='icon' href='data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220 0 32 32%22%3E%3Crect x=%221%22 y=%221%22 width=%2230%22 height=%2230%22 rx=%222%22 fill=%22%23000%22 stroke=%22%233B82F6%22/%3E%3Ctext x=%2216%22 y=%2222%22 text-anchor=%22middle%22 font-size=%2218%22 font-family=%22sans-serif%22 fill=%22%233B82F6%22%3EG%3C/text%3E%3C/svg%3E'>", -1);
    httpd_resp_send_chunk(req, appliance_styles, -1);
    httpd_resp_send_chunk(req, "</head><body><div class='layout'><aside class='sidebar'><div class='brand'><span class='brand-mark' aria-hidden='true'>G</span><span>GateMCU</span><select class='mobile-menu' id='mobile-menu' aria-label='Navigate' onchange='location.href=this.value'><option value='/' data-tr='Genel bakış' data-en='Overview'>Overview</option><option value='/wireguard'>WireGuard</option><option value='/forwards' data-tr='Port yönlendirme' data-en='Port forwarding'>Port forwarding</option><option value='/settings' data-tr='Wi-Fi ve ağ' data-en='Wi-Fi & Network'>Wi-Fi & Network</option><option value='/system' data-tr='Cihaz durumu' data-en='Device status'>Device status</option></select></div><div class='nav-label' data-tr='YÖNETİM' data-en='MANAGEMENT'>MANAGEMENT</div><nav class='nav-list' aria-label='Primary navigation'>", -1);
    send_nav_link(req, "/", "▦", "Genel bakış", "Overview", strcmp(screen, "overview") == 0);
    send_nav_link(req, "/wireguard", "⌁", "WireGuard", "WireGuard", strcmp(screen, "wireguard") == 0);
    send_nav_link(req, "/forwards", "⇄", "Port yönlendirme", "Port forwarding", strcmp(screen, "forwards") == 0);
    send_nav_link(req, "/settings", "⌘", "Wi-Fi ve ağ", "Wi-Fi & Network", strcmp(screen, "settings") == 0);
    send_nav_link(req, "/system", "≡", "Cihaz durumu", "Device status", strcmp(screen, "system") == 0);
    httpd_resp_send_chunk(req, "</nav><div class='sidebar-foot'>GateMCU · ESP8266</div></aside><div class='workspace'><header class='topbar'><div class='crumb'>GateMCU <span aria-hidden='true'>/</span> <span>", -1);
    httpd_resp_send_chunk(req, title_en, -1);
    httpd_resp_send_chunk(req, "</span></div><div class='top-actions'><span id='connection-state' class='status'>—</span><select class='lang' id='lang' aria-label='Language' onchange='setLanguage(this.value)'><option value='en'>EN</option><option value='tr'>TR</option></select><form method='post' action='/logout'><button class='logout' type='submit' data-tr='Çıkış' data-en='Sign out'>Sign out</button></form></div></header><main class='page' id='screen-content'>", -1);

    if (strcmp(screen, "overview") == 0) {
        httpd_resp_send_chunk(req, "<div class='page-heading'><div><h1 data-tr='Genel bakış' data-en='Overview'>Overview</h1><p class='lede' data-tr='Ağ geçidi ve tünel durumunun özeti.' data-en='A concise view of gateway and tunnel status.'>A concise view of gateway and tunnel status.</p></div></div><section class='panel' aria-labelledby='gateway-heading'><div class='panel-head'><h2 id='gateway-heading' data-tr='Bağlantı' data-en='Connectivity'>Connectivity</h2><span id='gateway-state' class='status'>—</span></div><div class='data-grid'><div class='data-cell'><span class='data-label' data-tr='Wi-Fi IP adresi' data-en='Wi-Fi address'>Wi-Fi address</span><strong class='data-value' id='ip'>—</strong></div><div class='data-cell'><span class='data-label' data-tr='VPN adresi' data-en='VPN address'>VPN address</span><strong class='data-value' id='tunnel-overview'>—</strong></div><div class='data-cell'><span class='data-label'>WireGuard</span><strong class='data-value' id='wg-status'>—</strong></div><div class='data-cell'><span class='data-label' data-tr='Çalışma süresi' data-en='Uptime'>Uptime</span><strong class='data-value' id='uptime-overview'>—</strong></div></div></section><section class='panel' aria-labelledby='usage-heading'><div class='panel-head'><h2 id='usage-heading' data-tr='Kaynak kullanımı' data-en='Resource usage'>Resource usage</h2><span class='status' id='usage-updated' data-tr='Canlı' data-en='Live'>Live</span></div><div class='panel-body'><div class='usage-list'><div class='usage-row'><div class='usage-head'><span class='usage-name'>CPU</span><span class='usage-value' id='cpu-label'>—</span></div><div class='track' role='progressbar' aria-label='CPU usage' aria-valuemin='0' aria-valuemax='100' aria-valuenow='0'><div class='fill' id='cpu-bar'></div></div></div><div class='usage-row'><div class='usage-head'><span class='usage-name'>RAM</span><span class='usage-value' id='ram-label'>—</span></div><div class='track' role='progressbar' aria-label='RAM usage' aria-valuemin='0' aria-valuemax='100' aria-valuenow='0'><div class='fill success' id='ram-bar'></div></div></div><div class='usage-row'><div class='usage-head'><span class='usage-name' data-tr='Uygulama depolaması' data-en='Application storage'>Application storage</span><span class='usage-value' id='storage-label'>—</span></div><div class='track' role='progressbar' aria-label='Application storage usage' aria-valuemin='0' aria-valuemax='100' aria-valuenow='0'><div class='fill' id='storage-bar'></div></div></div></div></div></section><section class='panel'><div class='panel-head'><h2 data-tr='Port yönlendirme' data-en='Port forwarding'>Port forwarding</h2><a href='/forwards' class='button' data-tr='Kuralları görüntüle' data-en='View rules'>View rules</a></div><div class='data-grid'><div class='data-cell'><span class='data-label' data-tr='Etkin kural' data-en='Active rules'>Active rules</span><strong class='data-value' id='rule-count'>0</strong></div><div class='data-cell'><span class='data-label' data-tr='Desteklenen protokol' data-en='Supported protocol'>Supported protocol</span><strong class='data-value'>TCP</strong></div></div></section>", -1);
    } else if (strcmp(screen, "wireguard") == 0) {
        httpd_resp_send_chunk(req, "<div class='page-heading'><div><h1>WireGuard</h1><p class='lede' data-tr='Tünel yapılandırmasını görüntüle ve bağlantıyı yönet.' data-en='View tunnel configuration and manage the connection.'>View tunnel configuration and manage the connection.</p></div><span id='wg-status' class='status'>—</span></div><section class='panel'><div class='panel-head'><h2 data-tr='İstemci yapılandırması' data-en='Client configuration'>Client configuration</h2><div class='inline-actions'>", -1);
        if (wg_configured) {
            httpd_resp_send_chunk(req, "<button class='button primary' id='wg-toggle' data-enabled='", -1);
            httpd_resp_send_chunk(req, wg_enabled ? "1" : "0", -1);
            httpd_resp_send_chunk(req, wg_enabled ? "' data-tr='Devre dışı bırak' data-en='Deactivate'>Deactivate</button>" : "' data-tr='Etkinleştir' data-en='Activate'>Activate</button>", -1);
            httpd_resp_send_chunk(req, "<button class='button' id='wg-edit' type='button' data-tr='Düzenle' data-en='Edit'>Edit</button><button class='button' id='wg-cancel' type='button' style='display:none' data-tr='İptal' data-en='Cancel'>Cancel</button>", -1);
        }
        httpd_resp_send_chunk(req, "</div></div><div class='panel-body'><p class='notice' data-tr='Yapılandırma gizli anahtar içerir ve yalnızca oturum açıldıktan sonra görüntülenir.' data-en='This configuration contains private keys and is visible only after sign-in.'>This configuration contains private keys and is visible only after sign-in.</p><form id='wg-form'><div class='field'><label for='wg-config' data-tr='WireGuard yapılandırması' data-en='WireGuard configuration'>WireGuard configuration</label><textarea class='input' id='wg-config' name='config' spellcheck='false' aria-describedby='wg-note' placeholder='[Interface]&#10;PrivateKey = ...&#10;Address = ...&#10;&#10;[Peer]&#10;PublicKey = ...&#10;Endpoint = host:51820'", -1);
        if (wg_configured) httpd_resp_send_chunk(req, " readonly", -1);
        httpd_resp_send_chunk(req, ">", -1);
        if (wg_configured) send_html_escaped(req, wg_config_text);
        httpd_resp_send_chunk(req, "</textarea><p class='field-note' id='wg-note' data-tr='WireGuard .conf içeriğini yapıştır. Kaydedilince tünel başlatılır.' data-en='Paste the contents of a WireGuard .conf file. Saving starts the tunnel.'>Paste the contents of a WireGuard .conf file. Saving starts the tunnel.</p></div><button class='button primary' id='wg-save' type='submit'", -1);
        if (wg_configured) httpd_resp_send_chunk(req, " hidden", -1);
        httpd_resp_send_chunk(req, " data-tr='Yapılandırmayı kaydet' data-en='Save configuration'>Save configuration</button></form></div></section>", -1);
    } else if (strcmp(screen, "forwards") == 0) {
        httpd_resp_send_chunk(req, "<div class='page-heading'><div><h1 data-tr='Port yönlendirme' data-en='Port forwarding'>Port forwarding</h1><p class='lede' data-tr='VPN üzerinden gelen TCP bağlantılarını yerel ağdaki cihazlara aktar.' data-en='Forward incoming TCP connections from the VPN to devices on your LAN.'>Forward incoming TCP connections from the VPN to devices on your LAN.</p></div></div><section class='panel'><div class='panel-head'><h2 data-tr='Kural ekle' data-en='Add a rule'>Add a rule</h2><span class='status' id='rule-capacity'>3 max</span></div><div class='panel-body'><form id='forward-form'><div class='form-grid'><div class='field'><label for='listen-port' data-tr='VPN giriş portu' data-en='VPN listen port'>VPN listen port</label><input class='input' id='listen-port' name='listen_port' type='number' min='1' max='65535' inputmode='numeric' required></div><div class='field'><label for='target-port' data-tr='Hedef port' data-en='Target port'>Target port</label><input class='input' id='target-port' name='target_port' type='number' min='1' max='65535' inputmode='numeric' required></div><div class='field'><label for='target-ip' data-tr='Hedef IPv4 adresi' data-en='Target IPv4 address'>Target IPv4 address</label><input class='input' id='target-ip' name='target_ip' placeholder='192.168.1.20' inputmode='decimal' required></div><div class='field'><label data-tr='Protokol' data-en='Protocol'>Protocol</label><input class='input' value='TCP' readonly aria-label='TCP protocol'></div></div><div class='notice warning' data-tr='Bu firmware TCP yönlendirmeyi destekler. UDP yönlendirme henüz uygulanmadı.' data-en='This firmware supports TCP forwarding. UDP forwarding is not implemented.'>This firmware supports TCP forwarding. UDP forwarding is not implemented.</div><button class='button primary' type='submit' data-tr='Kuralı ekle' data-en='Add rule'>Add rule</button></form></div></section><section class='panel'><div class='panel-head'><h2 data-tr='Yönlendirme kuralları' data-en='Forwarding rules'>Forwarding rules</h2><span class='status' id='rule-count'>0</span></div><div class='table-wrap'><table aria-label='Forwarding rules'><thead><tr><th style='width:23%' data-tr='Protokol' data-en='Protocol'>Protocol</th><th style='width:30%' data-tr='Giriş portu' data-en='Listen port'>Listen port</th><th data-tr='Hedef' data-en='Destination'>Destination</th></tr></thead><tbody id='rules'><tr><td colspan='3' class='empty-row' data-tr='Henüz kural eklenmedi.' data-en='No forwarding rules yet.'>No forwarding rules yet.</td></tr></tbody></table></div></section>", -1);
    } else if (strcmp(screen, "system") == 0) {
        httpd_resp_send_chunk(req, "<div class='page-heading'><div><h1 data-tr='Cihaz durumu' data-en='Device status'>Device status</h1><p class='lede' data-tr='Cihaz kaynakları ve çalışma süresi.' data-en='Device resources and uptime.'>Device resources and uptime.</p></div></div><section class='panel'><div class='panel-head'><h2 data-tr='İşlemci' data-en='Processor'>Processor</h2></div><div class='data-grid'><div class='data-cell'><span class='data-label' data-tr='Anlık kullanım' data-en='Current usage'>Current usage</span><strong class='data-value' id='cpu-load'>—</strong></div><div class='data-cell'><span class='data-label' data-tr='Saat hızı' data-en='Clock speed'>Clock speed</span><strong class='data-value' id='cpu-speed'>—</strong></div><div class='data-cell'><span class='data-label' data-tr='Görev sayısı' data-en='Task count'>Task count</span><strong class='data-value' id='cpu-tasks'>—</strong></div><div class='data-cell'><span class='data-label' data-tr='Çalışma süresi' data-en='Uptime'>Uptime</span><strong class='data-value' id='uptime'>—</strong></div></div></section><section class='panel'><div class='panel-head'><h2>RAM</h2></div><div class='data-grid'><div class='data-cell'><span class='data-label' data-tr='Toplam' data-en='Total'>Total</span><strong class='data-value' id='heap-total'>—</strong></div><div class='data-cell'><span class='data-label' data-tr='Boş' data-en='Free'>Free</span><strong class='data-value' id='heap-free'>—</strong></div><div class='data-cell'><span class='data-label' data-tr='En düşük boş miktar' data-en='Minimum free'>Minimum free</span><strong class='data-value' id='heap-min'>—</strong></div></div></section><section class='panel'><div class='panel-head'><h2 data-tr='Flash depolama' data-en='Flash storage'>Flash storage</h2></div><div class='data-grid'><div class='data-cell'><span class='data-label' data-tr='Toplam flash' data-en='Total flash'>Total flash</span><strong class='data-value' id='flash-total'>—</strong></div><div class='data-cell'><span class='data-label' data-tr='Uygulama kullanımı' data-en='Application used'>Application used</span><strong class='data-value' id='flash-used'>—</strong></div><div class='data-cell'><span class='data-label' data-tr='Uygulama bölümü' data-en='Application partition'>Application partition</span><strong class='data-value' id='flash-app'>—</strong></div><div class='data-cell'><span class='data-label' data-tr='NVS bölümü' data-en='NVS partition'>NVS partition</span><strong class='data-value' id='flash-nvs'>—</strong></div></div></section><p class='field-note' data-tr='Telemetri cihazdan anlık olarak okunur.' data-en='Telemetry is read from the device in real time.'>Telemetry is read from the device in real time.</p>", -1);
    } else {
        httpd_resp_send_chunk(req, "<div class='page-heading'><div><h1 data-tr='Wi-Fi ve ağ' data-en='Wi-Fi & Network'>Wi-Fi & Network</h1><p class='lede' data-tr='Kablosuz ağ, IP adresi ve yönetim parolasını yapılandır.' data-en='Configure wireless access, IP addressing and the admin password.'>Configure wireless access, IP addressing and the admin password.</p></div></div><section class='panel'><div class='panel-head'><h2 data-tr='Wi-Fi bağlantısı' data-en='Wi-Fi connection'>Wi-Fi connection</h2><span id='settings-connection' class='status'>—</span></div><div class='panel-body'><form id='settings-wifi'><div class='form-grid'><div class='field'><label for='wifi-ssid' data-tr='Ağ adı (SSID)' data-en='Network name (SSID)'>Network name (SSID)</label><input class='input' id='wifi-ssid' name='ssid' maxlength='32' autocomplete='ssid' required></div><div class='field'><label for='wifi-password' data-tr='Wi-Fi parolası' data-en='Wi-Fi password'>Wi-Fi password</label><input class='input' id='wifi-password' name='password' type='password' maxlength='63' autocomplete='new-password' placeholder='••••••••'></div><div class='field'><label for='ip-mode' data-tr='IP ataması' data-en='IP assignment'>IP assignment</label><select class='input' id='ip-mode' name='ip_mode'><option value='dhcp'>DHCP</option><option value='static' data-tr='Sabit IP' data-en='Static IP'>Static IP</option></select></div><div class='field'><label data-tr='Mevcut IP adresi' data-en='Current IP address'>Current IP address</label><input class='input' id='settings-current-ip' value='—' readonly></div></div><div id='static-fields' class='form-grid' hidden><div class='field'><label for='static-ip' data-tr='Cihaz IP adresi' data-en='Device IP address'>Device IP address</label><input class='input' id='static-ip' name='ip' placeholder='192.168.1.20' inputmode='decimal'></div><div class='field'><label for='static-gateway' data-tr='Ağ geçidi' data-en='Gateway'>Gateway</label><input class='input' id='static-gateway' name='gateway' placeholder='192.168.1.1' inputmode='decimal'></div><div class='field'><label for='static-netmask' data-tr='Alt ağ maskesi' data-en='Subnet mask'>Subnet mask</label><input class='input' id='static-netmask' name='netmask' placeholder='255.255.255.0' inputmode='decimal'></div></div><p class='field-note' data-tr='DHCP son verilen adresi hatırlamayı dener. Sabit IP için modem DHCP havuzunun dışında, kullanılmayan bir adres seç.' data-en='DHCP tries to restore its last lease. For a static address, choose an unused IP outside the router DHCP pool.'>DHCP tries to restore its last lease. For a static address, choose an unused IP outside the router DHCP pool.</p><div class='button-row'><button class='button primary' type='submit' data-tr='Ağ ayarlarını kaydet' data-en='Save network settings'>Save network settings</button></div></form></div></section><section class='panel'><div class='panel-head'><h2 data-tr='Yönetim parolası' data-en='Admin password'>Admin password</h2></div><div class='panel-body'><form id='password-form'><div class='form-grid'><div class='field'><label for='new-password' data-tr='Yeni parola (en az 8 karakter)' data-en='New password (at least 8 characters)'>New password (at least 8 characters)</label><input class='input' id='new-password' name='password' type='password' minlength='8' maxlength='63' autocomplete='new-password' required></div><div class='field'><label for='confirm-password' data-tr='Yeni parolayı tekrar gir' data-en='Confirm new password'>Confirm new password</label><input class='input' id='confirm-password' name='confirm' type='password' minlength='8' maxlength='63' autocomplete='new-password' required></div></div><button class='button primary' type='submit' data-tr='Parolayı güncelle' data-en='Update password'>Update password</button></form></div></section><section class='panel'><div class='panel-head'><h2 data-tr='Cihaz' data-en='Device'>Device</h2></div><div class='panel-body'><p class='notice' data-tr='Yeniden başlatma kayıtlı ağ ve VPN ayarlarını korur.' data-en='Restarting keeps saved Wi-Fi and VPN settings.'>Restarting keeps saved Wi-Fi and VPN settings.</p><form id='restart-form'><button class='button danger' type='submit' data-tr='Cihazı yeniden başlat' data-en='Restart device'>Restart device</button></form></div></section>", -1);
    }

    httpd_resp_send_chunk(req, "</main></div></div><div id='toast-region' class='toast-region' aria-live='polite'></div><script>const en=()=>document.documentElement.lang==='en';function setLanguage(l){document.documentElement.lang=l;localStorage.setItem('lang',l);document.querySelectorAll('[data-tr]').forEach(e=>e.textContent=e.dataset[l==='tr'?'tr':'en']);let s=document.getElementById('lang');if(s)s.value=l;let m=document.getElementById('mobile-menu');if(m)m.value=location.pathname;}function toast(message,type='ok'){let b=document.createElement('div');b.className='toast '+type;b.setAttribute('role','status');b.textContent=message;document.getElementById('toast-region').appendChild(b);setTimeout(()=>b.remove(),4000);}function fmtBytes(n){return n>=1048576?(n/1048576).toFixed(2)+' MiB':n>=1024?(n/1024).toFixed(1)+' KiB':n+' B';}function fmtUptime(n){let d=Math.floor(n/86400),h=Math.floor(n%86400/3600),m=Math.floor(n%3600/60);return (d?d+'d ':'')+h+'h '+m+'m';}function setText(id,value){let e=document.getElementById(id);if(e)e.textContent=value;}function progress(id,labelId,used,total,display){let pct=total?Math.max(0,Math.min(100,Math.round(used*100/total))):0;let bar=document.getElementById(id),label=document.getElementById(labelId);if(bar){bar.style.width=pct+'%';bar.parentElement.setAttribute('aria-valuenow',pct);}if(label)label.textContent=display+' · '+pct+'%';}function stateLabel(state){return state==='connected'?(en()?'Connected':'Bağlı'):state==='connecting'?(en()?'Connecting':'Bağlanıyor'):state==='failed'?(en()?'Failed':'Başarısız'):(en()?'Not connected':'Bağlı değil');}function setStatus(id,state){let e=document.getElementById(id);if(!e)return;e.className='status '+(state==='connected'?'ok':state==='failed'?'bad':state==='connecting'?'warn':'');e.textContent=stateLabel(state);}function renderRules(items){let body=document.getElementById('rules');if(!body)return;body.replaceChildren();if(!items.length){let row=document.createElement('tr'),cell=document.createElement('td');cell.colSpan=3;cell.className='empty-row';cell.textContent=en()?'No forwarding rules yet.':'Henüz kural eklenmedi.';row.appendChild(cell);body.appendChild(row);return;}items.forEach(rule=>{let row=document.createElement('tr');[ 'TCP',String(rule.listen_port),rule.target_ip+':'+rule.target_port ].forEach((value,index)=>{let cell=document.createElement('td');cell.textContent=value;if(index===0)cell.innerHTML='<strong>TCP</strong>';row.appendChild(cell);});body.appendChild(row);});}async function refresh(){try{let r=await fetch('/status',{cache:'no-store'});if(r.redirected||r.status===303){location.href='/login';return;}if(!r.ok)return;let d=await r.json(),rules=Array.isArray(d.rules)?d.rules:[];setText('ip',d.ip||'—');setText('settings-current-ip',d.ip||'—');setText('tunnel-overview',d.tunnel_ip||'—');setText('uptime-overview',fmtUptime(d.uptime||0));setText('rule-count',String(rules.length));renderRules(rules);setStatus('connection-state',d.state);setStatus('gateway-state',d.state);setStatus('settings-connection',d.state);let w=d.wg||'not_configured',we=document.getElementById('wg-status');if(we){we.className='status '+(w==='connected'?'ok':w==='failed'?'bad':w==='connecting'?'warn':'');we.textContent=w==='connected'?(en()?'Connected':'Bağlı'):w==='connecting'?(en()?'Connecting':'Bağlanıyor'):w==='disconnecting'?(en()?'Disconnecting':'Kapatılıyor'):w==='inactive'?(en()?'Inactive':'Kapalı'):w==='failed'?(en()?'Failed':'Başarısız'):(en()?'Not configured':'Yapılandırılmadı');}let usage=Math.max(0,Math.min(100,d.cpu_load||0)),ramUsed=Math.max(0,(d.heap_total||0)-(d.heap_free||0));progress('cpu-bar','cpu-label',usage,100,usage+'%');progress('ram-bar','ram-label',ramUsed,d.heap_total||0,fmtBytes(ramUsed)+' / '+fmtBytes(d.heap_total||0));progress('storage-bar','storage-label',d.app_bytes||0,d.app_capacity||0,fmtBytes(d.app_bytes||0)+' / '+fmtBytes(d.app_capacity||0));setText('cpu-load',(d.cpu_load||0)+'%');setText('cpu-speed',(d.cpu_mhz||0)+' MHz');setText('cpu-tasks',String(d.cpu_tasks||0));setText('heap-total',fmtBytes(d.heap_total||0));setText('heap-free',fmtBytes(d.heap_free||0));setText('heap-min',fmtBytes(d.heap_min||0));setText('flash-total',fmtBytes(d.flash_bytes||0));setText('flash-used',fmtBytes(d.app_bytes||0));setText('flash-app',fmtBytes(d.app_capacity||0));setText('flash-nvs',fmtBytes(d.nvs_bytes||0));setText('uptime',fmtUptime(d.uptime||0));let toggle=document.getElementById('wg-toggle');if(toggle){toggle.dataset.enabled=d.wg_enabled?'1':'0';toggle.textContent=d.wg_enabled?(en()?'Deactivate':'Devre dışı bırak'):(en()?'Activate':'Etkinleştir');}let form=document.getElementById('settings-wifi');if(form&&!form.dataset.loaded){document.getElementById('wifi-ssid').value=d.ssid||'';document.getElementById('ip-mode').value=d.ip_mode||'dhcp';document.getElementById('static-ip').value=d.static_ip||'';document.getElementById('static-gateway').value=d.gateway||'';document.getElementById('static-netmask').value=d.netmask||'';form.dataset.loaded='1';updateIpFields();}}catch(_){}}function updateIpFields(){let mode=document.getElementById('ip-mode'),fields=document.getElementById('static-fields');if(!mode||!fields)return;let fixed=mode.value==='static';fields.hidden=!fixed;fields.querySelectorAll('input').forEach(i=>i.required=fixed);}function setLanguage(l){document.documentElement.lang=l;localStorage.setItem('lang',l);document.querySelectorAll('[data-tr]').forEach(e=>e.textContent=e.dataset[l==='tr'?'tr':'en']);let s=document.getElementById('lang');if(s)s.value=l;let m=document.getElementById('mobile-menu');if(m)m.value=location.pathname;}let currentLang=localStorage.getItem('lang')||'en';document.getElementById('lang').value=currentLang;setLanguage(currentLang);let menu=document.getElementById('mobile-menu');if(menu)menu.value=location.pathname;let ipMode=document.getElementById('ip-mode');if(ipMode)ipMode.addEventListener('change',updateIpFields);", -1);
    httpd_resp_send_chunk(req, "let wf=document.getElementById('wg-form');if(wf)wf.addEventListener('submit',async e=>{e.preventDefault();let r=await fetch('/wireguard',{method:'POST',body:new URLSearchParams(new FormData(wf))});if(r.ok){toast(en()?'Configuration saved. Connecting…':'Yapılandırma kaydedildi. Bağlanıyor…','warn');setTimeout(()=>location.reload(),700);}else toast(await r.text(),'error');});let we=document.getElementById('wg-edit'),wc=document.getElementById('wg-cancel'),wt=document.getElementById('wg-toggle'),ta=document.getElementById('wg-config'),save=document.getElementById('wg-save');if(we)we.addEventListener('click',()=>{ta.readOnly=false;we.hidden=true;wc.hidden=false;save.hidden=false;ta.focus();});if(wc)wc.addEventListener('click',()=>location.reload());if(wt)wt.addEventListener('click',async()=>{let enabled=wt.dataset.enabled==='1'?'0':'1',r=await fetch('/wireguard/toggle',{method:'POST',body:new URLSearchParams({enabled})});if(r.ok){toast(enabled==='1'?(en()?'Activating WireGuard…':'WireGuard etkinleştiriliyor…'):(en()?'Deactivating WireGuard…':'WireGuard kapatılıyor…'),'warn');setTimeout(refresh,400);}else toast(await r.text(),'error');});let ff=document.getElementById('forward-form');if(ff)ff.addEventListener('submit',async e=>{e.preventDefault();let r=await fetch('/forward',{method:'POST',body:new URLSearchParams(new FormData(ff))});if(r.ok){toast(en()?'TCP rule added.':'TCP kuralı eklendi.');ff.reset();refresh();}else toast(await r.text(),'error');});let sw=document.getElementById('settings-wifi');if(sw)sw.addEventListener('submit',async e=>{e.preventDefault();let r=await fetch('/settings/wifi',{method:'POST',body:new URLSearchParams(new FormData(sw))});if(!r.ok){toast(await r.text(),'error');return;}let button=sw.querySelector('[type=submit]');button.disabled=true;toast(en()?'Applying network settings…':'Ağ ayarları uygulanıyor…','warn');let count=0,timer=setInterval(async()=>{count++;try{let d=await fetch('/status',{cache:'no-store'}).then(x=>x.json());if(d.state==='connected'){clearInterval(timer);button.disabled=false;toast((en()?'Connected at ':'Bağlandı · ')+(d.ip||'—'));refresh();}else if(d.state==='failed'||count>30){clearInterval(timer);button.disabled=false;toast(en()?'Could not connect. Check Wi-Fi and IP settings.':'Bağlantı kurulamadı. Wi-Fi ve IP ayarlarını kontrol et.','error');}}catch(_){if(count>30){clearInterval(timer);button.disabled=false;toast(en()?'Connection timed out. Reopen the page at the new IP.':'Bağlantı zaman aşımına uğradı. Yeni IP ile sayfayı aç.','error');}}},1000);});let pf=document.getElementById('password-form');if(pf)pf.addEventListener('submit',async e=>{e.preventDefault();let f=new FormData(pf),p=f.get('password');if(p!==f.get('confirm')){toast(en()?'Passwords do not match.':'Parolalar eşleşmiyor.','error');return;}let r=await fetch('/password',{method:'POST',body:new URLSearchParams({password:p})});if(r.ok){toast(en()?'Password updated.':'Parola güncellendi.');pf.reset();}else toast(await r.text(),'error');});let rf=document.getElementById('restart-form');if(rf)rf.addEventListener('submit',async e=>{e.preventDefault();if(!confirm(en()?'Restart GateMCU? Saved settings will be kept.':'GateMCU yeniden başlatılsın mı? Kayıtlı ayarlar korunacak.'))return;let r=await fetch('/restart',{method:'POST'});if(r.ok)toast(en()?'Restarting…':'Yeniden başlatılıyor…','warn');});refresh();setInterval(refresh,2000);</script></body></html>", -1);
    return httpd_resp_send_chunk(req, NULL, 0);
}


esp_err_t web_setup_page_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send_chunk(req, "<!doctype html><html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>GateMCU setup</title><link rel='icon' href='data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220 0 32 32%22%3E%3Crect x=%221%22 y=%221%22 width=%2230%22 height=%2230%22 rx=%222%22 fill=%22%23000%22 stroke=%22%233B82F6%22/%3E%3Ctext x=%2216%22 y=%2222%22 text-anchor=%22middle%22 font-size=%2218%22 font-family=%22sans-serif%22 fill=%22%233B82F6%22%3EG%3C/text%3E%3C/svg%3E'>", -1);
    httpd_resp_send_chunk(req, appliance_styles, -1);
    httpd_resp_send_chunk(req, "</head><body><header class='setup-top'><div class='brand'><span class='brand-mark' aria-hidden='true'>G</span>GateMCU</div><select class='lang' id='lang' aria-label='Language' onchange='setLanguage(this.value)'><option value='en'>EN</option><option value='tr'>TR</option></select></header><main class='setup-main'><div class='page-heading'><div><h1 id='title' data-tr='Ev ağına bağlan' data-en='Connect to your network'>Connect to your network</h1><p class='lede' id='subtitle' data-tr='GateMCU cihazını yapılandırmak için Wi-Fi bilgilerini gir.' data-en='Enter Wi-Fi details to set up GateMCU.'>Enter Wi-Fi details to set up GateMCU.</p></div></div><section class='panel setup-panel'><div class='panel-head'><h2 id='setup' data-tr='İlk kurulum' data-en='Initial setup'>Initial setup</h2></div><div class='panel-body'><form id='wifi-form' method='post' action='/wifi'><div class='field'><label id='ssid-label' for='ssid' data-tr='Ağ adı (SSID)' data-en='Network name (SSID)'>Network name (SSID)</label><input class='input' id='ssid' name='ssid' maxlength='32' autocomplete='ssid' required></div><div class='field'><label id='pass-label' for='wifi-pass' data-tr='Wi-Fi parolası' data-en='Wi-Fi password'>Wi-Fi password</label><input class='input' id='wifi-pass' name='password' type='password' maxlength='63' autocomplete='new-password'></div><div class='field'><label id='admin-label' for='admin-password' data-tr='Yönetici parolası (en az 8 karakter)' data-en='Admin password (at least 8 characters)'>Admin password (at least 8 characters)</label><input class='input' id='admin-password' name='admin_password' type='password' minlength='8' maxlength='63' autocomplete='new-password' required></div><div class='field'><label id='admin-confirm-label' for='admin-confirm' data-tr='Yönetici parolasını tekrar gir' data-en='Confirm admin password'>Confirm admin password</label><input class='input' id='admin-confirm' name='admin_confirm' type='password' minlength='8' maxlength='63' autocomplete='new-password' required></div><button class='button primary' id='save' type='submit' data-tr='Kaydet ve bağlan' data-en='Save and connect'>Save and connect</button></form><p class='field-note' id='help' data-tr='Sıfırlamak için FLASH düğmesini 5–7 saniye basılı tutup bırak.' data-en='Hold the FLASH button for 5–7 seconds, then release it to reset.'>Hold the FLASH button for 5–7 seconds, then release it to reset.</p><p class='field-note' id='internet-note' data-tr='Kurulum ağı internet sağlamaz. Bu sayfa bağlantı tamamlanana kadar açık kalmalı.' data-en='The setup network has no internet access. Keep this page open while GateMCU connects.'>The setup network has no internet access. Keep this page open while GateMCU connects.</p><div id='status' class='auth-error' role='status' aria-live='polite'></div></div></section></main><script>function setLanguage(l){document.documentElement.lang=l;localStorage.setItem('lang',l);document.querySelectorAll('[data-tr]').forEach(e=>e.textContent=e.dataset[l==='tr'?'tr':'en']);let s=document.getElementById('lang');if(s)s.value=l;}let l=localStorage.getItem('lang')||'en';document.getElementById('lang').value=l;setLanguage(l);document.getElementById('wifi-form').addEventListener('submit',async e=>{e.preventDefault();let f=new FormData(e.target),status=document.getElementById('status'),en=document.documentElement.lang==='en';if(f.get('admin_password')!==f.get('admin_confirm')){status.textContent=en?'Admin passwords do not match.':'Yönetici parolaları eşleşmiyor.';return;}status.textContent=en?'Connecting…':'Bağlanıyor…';let timer=setInterval(async()=>{try{let r=await fetch('/setup/status',{cache:'no-store'}),x=await r.json();if(x.state==='connected'){clearInterval(timer);status.textContent=(en?'Connected. Open http://':'Bağlandı. Aç: http://')+x.ip;document.getElementById('wifi-form').hidden=true;}else if(x.state==='failed'){clearInterval(timer);status.textContent=en?'Connection failed. Check the network name and password; setup access remains available.':'Bağlantı kurulamadı. Ağ adı ve parolayı kontrol et; kurulum ağı açık kalacak.';}}catch(_){}} ,750);try{let r=await fetch('/wifi',{method:'POST',body:new URLSearchParams(new FormData(e.target))});if(!r.ok){clearInterval(timer);status.textContent=await r.text();}}catch(_){}});</script></body></html>", -1);
    return httpd_resp_send_chunk(req, NULL, 0);
}


esp_err_t web_home_handler(httpd_req_t *req)
{
    if (gateway_wifi_state() != GATEWAY_WIFI_CONNECTED)
        return web_setup_page_handler(req);
    if (gateway_auth_configured() && !web_session_is_valid(req)) {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/login");
        return httpd_resp_send(req, "", 0);
    }
    return web_dashboard_page(req);
}

