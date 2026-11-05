use std::path::PathBuf;
use std::env;
use std::fs;

fn main() {
    println!("cargo:rerun-if-changed=c_libraries/libLZR.h");
    println!("cargo:rerun-if-changed=c_libraries/libLZR.c");
    println!("cargo:rerun-if-changed=c_libraries/kl4e.h");
    println!("cargo:rerun-if-changed=c_libraries/kl4e.c");

    cc::Build::new()
        .file("c_libraries/libLZR.c")
        .file("c_libraries/kl4e.c")
        .compile("libLZR");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    let bindings = r#"
extern "C" {
    pub fn LZRDecompress(
        out: *mut ::std::os::raw::c_void,
        out_capacity: ::std::os::raw::c_uint,
        in_: *mut ::std::os::raw::c_void,
        in_end: *mut *mut ::std::os::raw::c_void,
    ) -> ::std::os::raw::c_int;

    pub fn decompress_kle(
        outBuf: *mut u8,
        outSize: ::std::os::raw::c_int,
        inBuf: *mut u8,
        end: *mut *mut ::std::os::raw::c_void,
        isKl4e: ::std::os::raw::c_int,
    ) -> ::std::os::raw::c_int;
}
"#;
    fs::write(out_path.join("bindings.rs"), bindings).expect("Couldn't write bindings!");
}
