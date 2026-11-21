//! The `doppio-headless` command line.
//!
//! ```text
//! doppio-headless run <script.json|-> [options]   run a script, print a JSON report
//! doppio-headless session [options]               JSON lines in, JSON lines out
//! doppio-headless widgets [options]               list what is on screen
//! doppio-headless screenshot [options]            one screenshot of the fixture
//! doppio-headless catalog                         every catalogued widget id (JSON)
//! doppio-headless actions                         the action reference (JSON)
//! doppio-headless docs                            the action reference (Markdown)
//! ```
//!
//! Exit codes: `0` success, `1` a step/assertion failed, `2` usage or script
//! errors. Reports go to stdout, diagnostics to stderr.

use super::protocol::{Init, Script};
use super::{reference, Config, Driver, PROTOCOL_VERSION};
use crate::uikit::id;
use serde_json::{json, Value};
use std::io::{BufRead, Read, Write};
use std::path::PathBuf;

const HELP: &str = "\
doppio-headless — drive the Doppio editor UI without a window

USAGE
  doppio-headless run <script.json|dir|->...  Run scripts (files, folders of *.json, or - for stdin)
  doppio-headless session               Read one JSON step per line from stdin,
                                        answer with one JSON result per line
  doppio-headless widgets               List the widgets of the first frame
  doppio-headless screenshot            Render the starting fixture to a PNG
  doppio-headless catalog               Print every catalogued widget id (JSON)
  doppio-headless schema                Print the JSON Schema of a script
  doppio-headless actions               Print the action reference (JSON)
  doppio-headless docs                  Print the action reference (Markdown)
  doppio-headless help                  This text

OPTIONS (run, session, widgets, screenshot)
  --out <dir>           Where screenshots/artifacts go      [doppio-headless-out]
  --size <WxH>          Window size in logical pixels       [1600x900]
  --fixture <name>      empty | entities                    [entities]
  --asset-root <dir|temp>  Folder the asset wizard writes to
  --baselines <dir>     Baseline images for visual comparison
  --update-baselines    Create/refresh baselines instead of failing
  --format <json|text>  Report format for `run`             [json]
  --name <stem>         File name for `screenshot`          [shot]
  --annotate            Annotate the `screenshot` with widget ids

Scripts may set `fixture`, `size`, `assets` and `asset_root` themselves; the
options above only provide defaults. See caffeine-website/content/en/docs/doppio/headless-testing.md.
Set ICED_TEST_BACKEND=tiny-skia to force the CPU renderer (CI without a GPU).
";

struct Options {
    config: Config,
    init: Init,
    format_text: bool,
    name: String,
    annotate: bool,
    positional: Vec<String>,
}

fn parse_options(args: &[String]) -> Result<Options, String> {
    let mut options = Options {
        config: Config::default(),
        init: Init::default(),
        format_text: false,
        name: "shot".to_owned(),
        annotate: false,
        positional: Vec::new(),
    };
    let mut iter = args.iter();
    while let Some(arg) = iter.next() {
        let mut value = |flag: &str| -> Result<String, String> {
            iter.next()
                .cloned()
                .ok_or_else(|| format!("{flag} needs a value"))
        };
        match arg.as_str() {
            "--out" => options.config.out_dir = PathBuf::from(value("--out")?),
            "--size" => {
                let text = value("--size")?;
                let (w, h) = text
                    .split_once(['x', 'X'])
                    .ok_or("--size expects WxH, e.g. 1280x720")?;
                let size = [
                    w.parse::<f32>().map_err(|_| "bad width in --size")?,
                    h.parse::<f32>().map_err(|_| "bad height in --size")?,
                ];
                options.config.size = size;
                options.init.size = Some(size);
            }
            "--fixture" => options.init.fixture = Some(value("--fixture")?),
            "--asset-root" => options.init.asset_root = Some(value("--asset-root")?),
            "--baselines" => options.config.baselines = Some(PathBuf::from(value("--baselines")?)),
            "--update-baselines" => options.config.update_baselines = true,
            "--format" => match value("--format")?.as_str() {
                "json" => options.format_text = false,
                "text" => options.format_text = true,
                other => return Err(format!("unknown --format `{other}` (json, text)")),
            },
            "--name" => options.name = value("--name")?,
            "--annotate" => options.annotate = true,
            flag if flag.starts_with("--") => return Err(format!("unknown option `{flag}`")),
            _ => options.positional.push(arg.clone()),
        }
    }
    Ok(options)
}

fn print_json(value: &Value) {
    println!(
        "{}",
        serde_json::to_string_pretty(value).unwrap_or_else(|_| value.to_string())
    );
}

fn catalog() -> Value {
    json!({
        "protocol": PROTOCOL_VERSION,
        "note": "Dynamic ids (list rows, tabs per pane…) share these prefixes; see `widgets` for the live ids.",
        "ids": id::CATALOG.iter().map(|meta| json!({
            "id": meta.id.key(),
            "kind": meta.kind,
            "description": meta.description,
        })).collect::<Vec<_>>(),
    })
}

/// Entry point used by the binary. Returns the process exit code.
pub fn run(args: Vec<String>) -> i32 {
    let Some(command) = args.first().map(String::as_str) else {
        eprint!("{HELP}");
        return 2;
    };
    let rest = &args[1..];
    match command {
        "help" | "--help" | "-h" => {
            print!("{HELP}");
            0
        }
        "actions" => {
            print_json(&reference::json());
            0
        }
        "docs" => {
            print!("{}", reference::markdown());
            0
        }
        "catalog" => {
            print_json(&catalog());
            0
        }
        "schema" => {
            print_json(&reference::schema());
            0
        }
        "run" | "session" | "widgets" | "screenshot" => {
            let options = match parse_options(rest) {
                Ok(options) => options,
                Err(error) => {
                    eprintln!("error: {error}\n\n{HELP}");
                    return 2;
                }
            };
            let _guard = super::ui_lock();
            match command {
                "run" => run_script_command(options),
                "session" => session_command(options),
                "widgets" => oneshot(options, json!({ "action": "widgets" })),
                _ => {
                    let mut step = json!({ "action": "screenshot", "name": options.name });
                    if options.annotate {
                        step["annotate"] = json!(true);
                    }
                    oneshot(options, step)
                }
            }
        }
        other => {
            eprintln!("error: unknown command `{other}`\n\n{HELP}");
            2
        }
    }
}

fn read_script(source: &str) -> Result<(String, Script), String> {
    let text = if source == "-" {
        let mut buffer = String::new();
        std::io::stdin()
            .read_to_string(&mut buffer)
            .map_err(|error| format!("reading stdin: {error}"))?;
        buffer
    } else {
        std::fs::read_to_string(source).map_err(|error| format!("{source}: {error}"))?
    };
    let script = serde_json::from_str(&text)
        .map_err(|error| format!("{source}: invalid script JSON: {error}"))?;
    Ok((source.to_owned(), script))
}

/// Expands folders into their `*.json` files (sorted).
fn expand_sources(sources: &[String]) -> Result<Vec<String>, String> {
    let mut out = Vec::new();
    for source in sources {
        let path = std::path::Path::new(source);
        if source != "-" && path.is_dir() {
            let mut files: Vec<String> = std::fs::read_dir(path)
                .map_err(|error| format!("{source}: {error}"))?
                .filter_map(Result::ok)
                .map(|entry| entry.path())
                .filter(|path| path.extension().is_some_and(|ext| ext == "json"))
                .map(|path| path.display().to_string())
                .collect();
            files.sort();
            out.extend(files);
        } else {
            out.push(source.clone());
        }
    }
    Ok(out)
}

fn run_script_command(options: Options) -> i32 {
    if options.positional.is_empty() {
        eprintln!("error: `run` needs a script path, a folder of scripts, or `-` for stdin");
        return 2;
    }
    let sources = match expand_sources(&options.positional) {
        Ok(sources) if !sources.is_empty() => sources,
        Ok(_) => {
            eprintln!("error: no scripts found");
            return 2;
        }
        Err(error) => {
            eprintln!("error: {error}");
            return 2;
        }
    };
    let many = sources.len() > 1;
    let mut reports = Vec::new();
    let mut all_ok = true;
    for source in &sources {
        let (_, script) = match read_script(source) {
            Ok(parsed) => parsed,
            Err(error) => {
                eprintln!("error: {error}");
                return 2;
            }
        };
        // CLI options are defaults; the script wins.
        let mut init = script.init.clone();
        init.fixture = init.fixture.or(options.init.fixture.clone());
        init.size = init.size.or(options.init.size);
        init.asset_root = init.asset_root.or(options.init.asset_root.clone());
        let mut config = options.config.clone();
        if many {
            // One sub-folder per script keeps artifacts from colliding.
            let stem = std::path::Path::new(source)
                .file_stem()
                .map(|s| s.to_string_lossy().into_owned())
                .unwrap_or_else(|| "script".to_owned());
            config.out_dir = config.out_dir.join(stem);
        }
        let mut driver = match Driver::new(config, &init) {
            Ok(driver) => driver,
            Err(error) => {
                eprintln!("error: {error}");
                return 2;
            }
        };
        let report = driver.run_script(&script, true);
        all_ok &= report.ok;
        reports.push(report);
    }
    if options.format_text {
        for report in &reports {
            print!("{}", report.text());
        }
    } else if many {
        print_json(&json!({
            "protocol": PROTOCOL_VERSION,
            "ok": all_ok,
            "reports": reports,
        }));
    } else {
        print_json(&serde_json::to_value(&reports[0]).unwrap_or(Value::Null));
    }
    if all_ok {
        0
    } else {
        1
    }
}

fn oneshot(options: Options, step: Value) -> i32 {
    let mut driver = match Driver::new(options.config.clone(), &options.init) {
        Ok(driver) => driver,
        Err(error) => {
            eprintln!("error: {error}");
            return 2;
        }
    };
    let result = driver.run_step(0, &step);
    print_json(&serde_json::to_value(&result).unwrap_or(Value::Null));
    if result.ok {
        0
    } else {
        1
    }
}

fn session_command(options: Options) -> i32 {
    let mut driver = match Driver::new(options.config.clone(), &options.init) {
        Ok(driver) => driver,
        Err(error) => {
            eprintln!("error: {error}");
            return 2;
        }
    };
    let stdout = std::io::stdout();
    let mut out = stdout.lock();
    let ready = json!({
        "event": "ready",
        "protocol": PROTOCOL_VERSION,
        "out_dir": driver.out_dir().display().to_string(),
        "actions": reference::ACTIONS.iter().map(|a| a.name).collect::<Vec<_>>(),
        "hint": "send one JSON step per line; {\"action\":\"quit\"} ends the session",
    });
    let _ = writeln!(out, "{ready}");
    let _ = out.flush();
    let stdin = std::io::stdin();
    let mut index = 0usize;
    let mut failures = 0usize;
    for line in stdin.lock().lines() {
        let Ok(line) = line else { break };
        let line = line.trim();
        if line.is_empty() {
            continue;
        }
        let value: Value = match serde_json::from_str(line) {
            Ok(value) => value,
            Err(error) => {
                let _ = writeln!(
                    out,
                    "{}",
                    json!({ "ok": false, "error": format!("invalid JSON: {error}") })
                );
                let _ = out.flush();
                continue;
            }
        };
        if value.get("action").and_then(Value::as_str) == Some("quit") {
            let _ = writeln!(
                out,
                "{}",
                json!({ "event": "bye", "steps": index, "failed": failures })
            );
            break;
        }
        let result = driver.run_step(index, &value);
        if !result.ok {
            failures += 1;
        }
        index += 1;
        let _ = writeln!(
            out,
            "{}",
            serde_json::to_string(&result).unwrap_or_else(|_| "{}".to_owned())
        );
        let _ = out.flush();
    }
    if failures == 0 {
        0
    } else {
        1
    }
}
