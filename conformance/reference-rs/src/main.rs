//! `reference-server -c config.json`: teta-wot v0.1.0 serving the twins,
//! with teta-wot's command line.

use std::process::ExitCode;

#[tokio::main]
async fn main() -> ExitCode {
    teta_wot::server::cli::serve_from_cli(&reference_rs::registry()).await
}
