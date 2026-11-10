// Playing together over the internet (decision 0057): the Online page's settings, the relay on this PC for the
// player who hosts, the address to give the others, and a test that a server can be reached.
//
//   <install>\online.ini   YW4_RELAY (host:port), YW4_ROOM, YW4_NICKNAME: the game reads it next to settings.ini
//   the relay              <builder>\python\python.exe <game code>\tools\relay\relay.py --port 41230, no window;
//                          it outlives the launcher (the game keeps playing); the page's button stops it.
//                          Its output: <install>\logs\relay.log, its process id: logs\relay.pid.

use serde::{Deserialize, Serialize};
use std::io::Write;
use std::net::{TcpStream, ToSocketAddrs};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::Duration;
use tauri::{AppHandle, Manager};

pub const PORT: u16 = 41230;

#[derive(Serialize, Deserialize, Default, Clone)]
pub struct Online {
    relay: String,  // host:port of the server ("" = playing alone)
    room: String,   // the room code: only players with the same one see each other's rooms
    name: String,   // the name the others see (at most 10 characters, as on a Switch)
}

fn ini_path(install: &str) -> PathBuf {
    Path::new(install).join("online.ini")
}

/// One value: printable, no line breaks or '=' (the file is name=value lines), at most `max` characters.
fn clean(s: &str, max: usize) -> String {
    s.chars().filter(|c| !c.is_control() && *c != '=').take(max).collect::<String>().trim().to_string()
}

#[tauri::command]
pub fn online_get(install: String) -> Online {
    let mut o = Online::default();
    if let Ok(text) = std::fs::read_to_string(ini_path(&install)) {
        for line in text.lines() {
            if let Some((k, v)) = line.split_once('=') {
                match k.trim() {
                    "YW4_RELAY" => o.relay = v.trim().to_string(),
                    "YW4_ROOM" => o.room = v.trim().to_string(),
                    "YW4_NICKNAME" => o.name = v.trim().to_string(),
                    _ => {}
                }
            }
        }
    }
    o
}

#[tauri::command]
pub fn online_set(install: String, relay: String, room: String, name: String) -> Result<(), String> {
    let mut relay = clean(&relay, 200).replace(' ', "");
    if !relay.is_empty() && !relay.contains(':') {
        relay = format!("{relay}:{PORT}");  // the address alone: the relay's own port
    }
    let text = format!(
        "# Written by the launcher's Online page (decision 0057): the server for playing together, the room code\n\
         # and the name the others see. Empty YW4_RELAY: local play stays on this PC.\n\
         YW4_RELAY={}\nYW4_ROOM={}\nYW4_NICKNAME={}\n",
        relay, clean(&room, 60), clean(&name, 10));
    let mut f = std::fs::File::create(ini_path(&install)).map_err(|e| e.to_string())?;
    f.write_all(text.as_bytes()).map_err(|e| e.to_string())
}

/// The relay on this PC answers on its port (started by this launcher now or earlier, which it outlives: closing
/// the launcher after starting the game mustn't drop the friends).
#[tauri::command]
pub fn relay_running() -> bool {
    TcpStream::connect_timeout(&([127, 0, 0, 1], PORT).into(), Duration::from_millis(300)).is_ok()
}

fn pid_file(install: &str) -> PathBuf {
    Path::new(install).join("logs").join("relay.pid")
}

/// Starts the relay on this PC (port 41230, every network interface), from the newest game code, without a window.
#[tauri::command]
pub fn relay_start(app: AppHandle, install: String) -> Result<(), String> {
    if relay_running() { return Ok(()); }
    let builder = app.path().resource_dir().map_err(|e| e.to_string())?;
    let builder = PathBuf::from(builder.to_string_lossy().trim_start_matches(r"\\?\")).join("builder");
    let python = builder.join("python").join("python.exe");
    let code = crate::update::game_code(&builder.join("src"), Path::new(&install));
    let script = code.dir.join("tools").join("relay").join("relay.py");
    if !python.is_file() || !script.is_file() {
        return Err("The server isn't in this launcher's game code yet: update the launcher.".into());
    }
    let logs = Path::new(&install).join("logs");
    let _ = std::fs::create_dir_all(&logs);
    let log = std::fs::File::create(logs.join("relay.log")).map_err(|e| e.to_string())?;
    let mut cmd = Command::new(&python);
    cmd.arg("-u").arg(&script).args(["--port", &PORT.to_string()])
        .stdout(Stdio::from(log.try_clone().map_err(|e| e.to_string())?)).stderr(Stdio::from(log)).stdin(Stdio::null());
    #[cfg(windows)]
    { use std::os::windows::process::CommandExt; cmd.creation_flags(0x0800_0000 | 0x0000_0008); }  // no window, detached
    let child = cmd.spawn().map_err(|e| format!("couldn't start the server: {e}"))?;
    let _ = std::fs::write(pid_file(&install), child.id().to_string());
    for _ in 0..30 {
        if relay_running() { return Ok(()); }
        std::thread::sleep(Duration::from_millis(100));
    }
    Err(r"The server didn't start: see logs\relay.log".into())
}

/// Stops the relay this launcher started (by the process id it wrote down).
#[tauri::command]
pub fn relay_stop(install: String) {
    let Ok(pid) = std::fs::read_to_string(pid_file(&install)) else { return };
    let pid = pid.trim();
    if pid.is_empty() || !pid.chars().all(|c| c.is_ascii_digit()) { return; }
    let mut cmd = Command::new(r"C:\Windows\System32\taskkill.exe");
    cmd.args(["/PID", pid, "/T", "/F"]);
    #[cfg(windows)]
    { use std::os::windows::process::CommandExt; cmd.creation_flags(0x0800_0000); }
    let _ = cmd.status();
    let _ = std::fs::remove_file(pid_file(&install));
}

/// This PC's address on the internet, as the others must type it (asked of api.ipify.org, which only answers it).
#[tauri::command]
pub async fn public_address() -> Result<String, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let mut cmd = Command::new(r"C:\Windows\System32\curl.exe");
        cmd.args(["-sS", "--max-time", "8", "https://api.ipify.org"]);
        #[cfg(windows)]
        { use std::os::windows::process::CommandExt; cmd.creation_flags(0x0800_0000); }
        let out = cmd.output().map_err(|e| e.to_string())?;
        let ip = String::from_utf8_lossy(&out.stdout).trim().to_string();
        if out.status.success() && !ip.is_empty() && ip.len() < 64 && ip.chars().all(|c| c.is_ascii_hexdigit() || c == '.' || c == ':') {
            Ok(ip)
        } else {
            Err("couldn't find out (no internet?)".into())
        }
    }).await.map_err(|e| e.to_string())?
}

/// Can this PC reach the server at host:port? "" if so, else why not.
#[tauri::command]
pub async fn relay_test(address: String) -> String {
    tauri::async_runtime::spawn_blocking(move || {
        let mut a = address.trim().to_string();
        if a.is_empty() { return "Enter the server's address first.".to_string(); }
        if !a.contains(':') { a = format!("{a}:{PORT}"); }
        let Ok(mut addrs) = a.to_socket_addrs() else { return format!("{a}: no such address") };
        match addrs.next() {
            Some(sa) => match TcpStream::connect_timeout(&sa, Duration::from_secs(5)) {
                Ok(_) => String::new(),
                Err(e) => format!("{a} doesn't answer ({e}). Is the server started there, and the port forwarded?"),
            },
            None => format!("{a}: no such address"),
        }
    }).await.unwrap_or_else(|e| e.to_string())
}
