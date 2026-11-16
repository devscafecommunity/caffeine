//! `doppio-headless`: scripted, human-like UI actions and screenshots without a
//! window. See `doppio/docs/headless-testing.md`.

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    std::process::exit(doppio_ui::windows::main::headless::cli::run(args));
}
