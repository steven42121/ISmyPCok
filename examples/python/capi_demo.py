import ctypes
from ctypes import c_char_p, c_int, POINTER
from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    dll_path = root / "build" / "Release" / "ispcok_capi.dll"
    lib = ctypes.CDLL(str(dll_path))

    lib.ispcok_version.restype = c_char_p
    lib.ispcok_run_modules.argtypes = [c_char_p, c_char_p, c_char_p, POINTER(c_char_p)]
    lib.ispcok_run_modules.restype = c_int
    lib.ispcok_free_string.argtypes = [c_char_p]

    out_json = c_char_p()
    rc = lib.ispcok_run_modules(
        b"cpu_fp32,memory_bw,net_bw,net_rtt",
        b"llm_infer_server",
        None,
        ctypes.byref(out_json),
    )
    if rc != 0:
        raise RuntimeError(f"ispcok_run_modules failed: {rc}")

    try:
        print("version:", lib.ispcok_version().decode("utf-8"))
        print(out_json.value.decode("utf-8"))
    finally:
        lib.ispcok_free_string(out_json)


if __name__ == "__main__":
    main()
