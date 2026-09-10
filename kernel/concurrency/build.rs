// build.rs — Cargo build script for obsidian_actor_runtime
// ---------------------------------------------------------------------------
// Runs cbindgen to generate a C-ABI header (actor_runtime.h) from the
// Rust source.  The generated header is placed in the crate's OUT_DIR
// and also copied to the source tree so CMake can find it at configure time.
// ---------------------------------------------------------------------------

use std::env;
use std::path::PathBuf;

fn main() {
    let crate_dir = env::var("CARGO_MANIFEST_DIR").unwrap();
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());

    // Only run cbindgen if the config file exists (graceful fallback)
    let config_path = PathBuf::from(&crate_dir).join("cbindgen.toml");
    let config = if config_path.exists() {
        cbindgen::Config::from_file(&config_path)
            .expect("Failed to read cbindgen.toml")
    } else {
        // Sensible defaults for C-ABI header generation
        let mut cfg = cbindgen::Config::default();
        cfg.language = cbindgen::Language::C;
        cfg.include_guard = Some("OBSIDIAN_ACTOR_RUNTIME_H".to_string());
        cfg.cpp_compat = true;
        cfg.style = cbindgen::Style::Both;
        cfg
    };

    let bindings = cbindgen::Builder::new()
        .with_crate(&crate_dir)
        .with_config(config)
        .generate();

    match bindings {
        Ok(b) => {
            // Write to OUT_DIR (Cargo's build directory)
            let header_path = out_dir.join("actor_runtime.h");
            b.write_to_file(&header_path);

            // Also write to source tree for CMake discoverability
            let src_header = PathBuf::from(&crate_dir).join("actor_runtime.h");
            b.write_to_file(&src_header);

            println!("cargo:rerun-if-changed=actor_runtime.rs");
            println!("cargo:rerun-if-changed=cbindgen.toml");
        }
        Err(e) => {
            // Don't fail the build if there are no extern "C" fns yet —
            // the source file is currently a stub.
            eprintln!("cbindgen skipped: {e}");
        }
    }
}
