#!/usr/bin/env python3
"""
benchmark_vmem.py
=================
Compara el rendimiento de las políticas de reemplazo de páginas en Nachos:
  - RANDOM  (sin flag)
  - FIFO    (-DPRPOLICY_FIFO)
  - CLOCK   (-DPRPOLICY_CLOCK)
  - ÓPTIMO  (calculado off-line a partir de la traza de accesos)

Programas evaluados: matmult, sort

Métricas recolectadas de la salida de Nachos:
  - Page faults totales
  - Lecturas a disco  (numDiskReads)
  - Escrituras a disco (numDiskWrites)
  - Swap reads / Swap writes
  - TLB hits / misses / hit ratio
  - Ticks totales, de usuario, de sistema

Algoritmo ÓPTIMO (Bélády):
  Se agrega un flag de compilación -DTRACE_PAGES que hace que Nachos imprima
  en stderr cada acceso de página virtual (prefijo "PTRACE <vpn>").
  El script captura esa traza, la procesa con el algoritmo OPT offline y
  reporta los fallos mínimos teóricos.

Uso:
  python3 benchmark_vmem.py [--frames N] [--runs R] [--skip-build]

  --frames N   : número de marcos de memoria física (default: usa el de Nachos)
  --runs R     : veces que se repite cada experimento para promediar (default: 3)
  --skip-build : omite la recompilación (usa el binario ya construido)
"""

import argparse
import os
import re
import subprocess
import sys
import shutil
from pathlib import Path
from collections import defaultdict
import statistics as stats_module


# ──────────────────────────────────────────────────────────────────────────────
# Configuración
# ──────────────────────────────────────────────────────────────────────────────

SCRIPT_DIR   = Path(__file__).parent.resolve()
VMEM_DIR     = SCRIPT_DIR / "vmem"
USERLAND_DIR = SCRIPT_DIR / "userland"
NACHOS_BIN   = VMEM_DIR / "nachos"

# Programas de usuario a evaluar
USER_PROGRAMS = ["matmult", "sort"]

# Políticas a evaluar
POLICIES = {
    "RANDOM":  "",
    "FIFO":    "-DPRPOLICY_FIFO",
    "CLOCK":   "-DPRPOLICY_CLOCK",
}

# Regex para parsear la salida de estadísticas de Nachos
STAT_PATTERNS = {
    "totalTicks":    re.compile(r"Ticks: total (\d+)"),
    "idleTicks":     re.compile(r"Ticks: total \d+, idle (\d+)"),
    "systemTicks":   re.compile(r"Ticks: total \d+, idle \d+, system (\d+)"),
    "userTicks":     re.compile(r"Ticks: total \d+, idle \d+, system \d+, user (\d+)"),
    "diskReads":     re.compile(r"Disk I/O: reads (\d+)"),
    "diskWrites":    re.compile(r"Disk I/O: reads \d+, writes (\d+)"),
    "pageFaults":    re.compile(r"Paging: faults (\d+)"),
    "swapReads":     re.compile(r"Swap: reads (\d+)"),
    "swapWrites":    re.compile(r"Swap: reads \d+, writes (\d+)"),
    "tlbHits":       re.compile(r"TLB: hits (\d+)"),
    "tlbMisses":     re.compile(r"TLB: hits \d+, misses (\d+)"),
    "tlbTotal":      re.compile(r"TLB: hits \d+, misses \d+, total (\d+)"),
    "tlbHitRatio":   re.compile(r"TLB: hits \d+, misses \d+, total \d+, hit ratio ([\d.]+)%"),
}

# Patrón para capturar la traza de accesos de página (emitida con -DTRACE_PAGES)
PTRACE_PATTERN = re.compile(r"^PTRACE (\d+)$", re.MULTILINE)


# ──────────────────────────────────────────────────────────────────────────────
# Utilidades
# ──────────────────────────────────────────────────────────────────────────────

def run(cmd: list[str], cwd: Path, capture: bool = True, timeout: int = 300):
    """Ejecuta un comando y devuelve (stdout, stderr, returncode)."""
    result = subprocess.run(
        cmd,
        cwd=str(cwd),
        capture_output=capture,
        text=True,
        timeout=timeout,
    )
    return result.stdout, result.stderr, result.returncode


def parse_stats(output: str) -> dict:
    """Extrae métricas de la salida combinada (stdout + stderr) de Nachos."""
    metrics = {}
    for key, pat in STAT_PATTERNS.items():
        m = pat.search(output)
        if m:
            val = m.group(1)
            try:
                metrics[key] = float(val) if "." in val else int(val)
            except ValueError:
                metrics[key] = val
        else:
            metrics[key] = None
    return metrics


def avg(lst):
    """Promedio de una lista de números, ignorando None."""
    clean = [v for v in lst if v is not None]
    return round(stats_module.mean(clean), 2) if clean else None


# ──────────────────────────────────────────────────────────────────────────────
# Compilación
# ──────────────────────────────────────────────────────────────────────────────

BASE_DEFINES = (
    "-DUSER_PROGRAM -DFILESYS_NEEDED -DFILESYS_STUB -DVMEM "
    "-DUSE_TLB -DDFS_TICKS_FIX -DDEMAND_LOADING -DSWAP"
)


def patch_makefile(policy_flag: str, extra_flags: str = ""):
    """
    Reescribe la línea DEFINES del Makefile de vmem para compilar con la
    política deseada.
    """
    makefile = VMEM_DIR / "Makefile"
    text = makefile.read_text(encoding="utf-8")

    defines_line = f"DEFINES      = {BASE_DEFINES}"
    if policy_flag:
        defines_line += f" {policy_flag}"
    if extra_flags:
        defines_line += f" {extra_flags}"

    new_text = re.sub(
        r"^DEFINES\s*=.*$",
        defines_line,
        text,
        flags=re.MULTILINE,
    )
    makefile.write_text(new_text, encoding="utf-8")


def build(policy_name: str, policy_flag: str, extra_flags: str = "") -> bool:
    """Compila Nachos con la política indicada. Retorna True si OK."""
    print(f"  [build] Compilando con política {policy_name}...", end=" ", flush=True)
    patch_makefile(policy_flag, extra_flags)

    stdout, stderr, rc = run(["make", "-j4"], cwd=VMEM_DIR)
    if rc != 0:
        print("ERROR")
        print(stderr[-2000:])
        return False
    print("OK")
    return True


# ──────────────────────────────────────────────────────────────────────────────
# Ejecución de Nachos
# ──────────────────────────────────────────────────────────────────────────────

def run_nachos(program: str, timeout: int = 300, num_frames: int = 0) -> tuple[str, str, int]:
    """
    Ejecuta `./nachos [-m num_frames] -x ../userland/<program>` en el directorio vmem.
    Retorna (stdout, stderr, returncode).
    """
    prog_path = f"../userland/{program}"
    cmd = [str(NACHOS_BIN)]
    if num_frames > 0:
        cmd += ["-m", str(num_frames)]
    cmd += ["-x", prog_path]
    result = subprocess.run(
        cmd,
        cwd=str(VMEM_DIR),
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    return result.stdout, result.stderr, result.returncode


# ──────────────────────────────────────────────────────────────────────────────
# Algoritmo ÓPTIMO (Bélády)
# ──────────────────────────────────────────────────────────────────────────────

def optimal_faults(trace: list[int], num_frames: int) -> int:
    """
    Simula el algoritmo OPT de Bélády sobre la traza dada.
    Retorna el número de fallos de página.
    """
    if num_frames <= 0:
        return len(set(trace))

    frames   = set()
    faults   = 0
    n        = len(trace)

    # Pre-computar: para cada posición i, la próxima vez que se usa trace[i]
    next_use: list[int] = []
    last_seen: dict[int, int] = {}
    for i in range(n - 1, -1, -1):
        page = trace[i]
        next_use.append(last_seen.get(page, n))  # n = "nunca más"
        last_seen[page] = i
    next_use.reverse()

    # Simulación
    frame_list = []  # para poder buscar la víctima de forma más fácil
    future = defaultdict(list)
    for i, page in enumerate(trace):
        future[page].append(i)

    frame_next = {}  # frame -> próxima referencia

    for i, page in enumerate(trace):
        # Actualizar "próxima referencia" en el mapa
        refs = future[page]
        if refs and refs[0] == i:
            refs.pop(0)
        next_ref = refs[0] if refs else n

        if page in frames:
            frame_next[page] = next_ref
            continue  # hit

        faults += 1
        if len(frames) < num_frames:
            frames.add(page)
            frame_next[page] = next_ref
        else:
            # Elegir la página que se usará más tarde
            victim = max(frame_next, key=lambda p: frame_next[p])
            frames.remove(victim)
            del frame_next[victim]
            frames.add(page)
            frame_next[page] = next_ref

    return faults


def collect_trace(program: str) -> list[int]:
    """
    Ejecuta Nachos con -DTRACE_PAGES y extrae la traza de páginas virtuales
    del stderr.  Si PTRACE no está disponible, devuelve lista vacía.
    """
    stdout, stderr, rc = run_nachos(program)
    combined = stdout + "\n" + stderr
    pages = [int(m) for m in PTRACE_PATTERN.findall(combined)]
    return pages


# ──────────────────────────────────────────────────────────────────────────────
# Benchmark principal
# ──────────────────────────────────────────────────────────────────────────────

def benchmark(program: str, policy_name: str, runs: int, skip_build: bool,
              policy_flag: str, extra_flags: str = "", num_frames: int = 0) -> dict:
    """Ejecuta `runs` veces Nachos y devuelve las métricas promediadas."""
    if not skip_build:
        ok = build(policy_name, policy_flag, extra_flags)
        if not ok:
            return {"error": "build failed"}

    results: dict[str, list] = defaultdict(list)
    for r in range(runs):
        stdout, stderr, rc = run_nachos(program, num_frames=num_frames)
        combined = stdout + "\n" + stderr
        m = parse_stats(combined)
        for k, v in m.items():
            if v is not None:
                results[k].append(v)

    return {k: avg(v) for k, v in results.items()}


# ──────────────────────────────────────────────────────────────────────────────
# Reporte
# ──────────────────────────────────────────────────────────────────────────────

COL_W = 12   # ancho de columna

def print_table(program: str, data: dict, opt_results: dict):
    """Imprime la tabla de comparación para un programa dado."""
    policies     = list(data.keys())
    all_policies = policies + ["OPTIMO"]

    metrics = [
        ("pageFaults",   "Fallos pág."),
        ("diskReads",    "Lecturas disco"),
        ("diskWrites",   "Escrituras disco"),
        ("swapReads",    "Swap reads"),
        ("swapWrites",   "Swap writes"),
        ("tlbHitRatio",  "TLB hit% "),
        ("tlbHits",      "TLB hits"),
        ("tlbMisses",    "TLB misses"),
        ("totalTicks",   "Ticks total"),
        ("userTicks",    "Ticks usuario"),
        ("systemTicks",  "Ticks sistema"),
    ]

    sep  = "─" * (20 + COL_W * len(all_policies) + len(all_policies))
    head = f"{'Métrica':<20}" + "".join(f"{p:>{COL_W}}" for p in all_policies)

    print(f"\n{'═'*len(sep)}")
    print(f"  PROGRAMA: {program.upper()}")
    print(f"{'═'*len(sep)}")
    print(head)
    print(sep)

    for key, label in metrics:
        row = f"{label:<20}"
        for pol in policies:
            val = data[pol].get(key)
            row += f"{str(val) if val is not None else 'N/A':>{COL_W}}"
        # Columna OPTIMO
        opt_val = opt_results.get(key)
        if opt_val is None and key == "pageFaults":
            opt_val = opt_results.get("opt_faults")
        row += f"{str(opt_val) if opt_val is not None else 'N/A':>{COL_W}}"
        print(row)

    print(sep)


def print_summary_comparison(all_data: dict):
    """Imprime un resumen final comparando todos los programas y políticas."""
    print("\n" + "═"*60)
    print("  RESUMEN – Fallos de página por programa y política")
    print("═"*60)
    header = f"{'Programa':<12}" + "".join(f"{p:>12}" for p in list(POLICIES.keys()) + ["OPTIMO"])
    print(header)
    print("─"*60)
    for prog, pol_data in all_data.items():
        row = f"{prog:<12}"
        for pol in POLICIES:
            val = pol_data.get(pol, {}).get("pageFaults")
            row += f"{str(val) if val is not None else 'N/A':>12}"
        opt = pol_data.get("OPTIMO", {}).get("opt_faults")
        row += f"{str(opt) if opt is not None else 'N/A':>12}"
        print(row)
    print("─"*60)


# ──────────────────────────────────────────────────────────────────────────────
# Detección del número de marcos
# ──────────────────────────────────────────────────────────────────────────────

def detect_num_frames() -> int:
    """
    Intenta leer el número de marcos de la configuración de Nachos.
    Busca NUM_PHYS_PAGES en machine/machine.hh.
    """
    machine_hh = SCRIPT_DIR / "machine" / "machine.hh"
    if machine_hh.exists():
        text = machine_hh.read_text(errors="ignore")
        m = re.search(r"NUM_PHYS_PAGES\s+(\d+)", text)
        if m:
            return int(m.group(1))
    return 32  # valor por defecto razonable


# ──────────────────────────────────────────────────────────────────────────────
# Captura de trazas con TRACE_PAGES
# ──────────────────────────────────────────────────────────────────────────────

def collect_trace_with_build(program: str, num_frames: int = 0) -> list[int]:
    """
    Recompila con -DTRACE_PAGES y ejecuta para capturar la traza.
    """
    print(f"  [trace] Compilando con TRACE_PAGES para {program}...", end=" ", flush=True)
    # Usamos RANDOM + TRACE_PAGES para la traza (la política no importa para OPT)
    patch_makefile("", extra_flags="-DTRACE_PAGES")
    stdout_b, stderr_b, rc = run(["make", "-j4"], cwd=VMEM_DIR)
    if rc != 0:
        print("ERROR (build)")
        return []
    print("OK")

    print(f"  [trace] Ejecutando {program} para capturar traza...", end=" ", flush=True)
    stdout, stderr, rc = run_nachos(program, timeout=600, num_frames=num_frames)
    combined = stdout + "\n" + stderr
    pages = [int(m) for m in PTRACE_PATTERN.findall(combined)]
    if pages:
        print(f"OK ({len(pages)} accesos)")
    else:
        print("sin traza PTRACE (¿TRACE_PAGES no implementado?)")
    return pages


# ──────────────────────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Benchmark de políticas de reemplazo de páginas en Nachos"
    )
    parser.add_argument(
        "--frames", type=int, default=None,
        help="Número de marcos físicos (default: detectado automáticamente)"
    )
    parser.add_argument(
        "--runs", type=int, default=3,
        help="Número de ejecuciones por experimento (default: 3)"
    )
    parser.add_argument(
        "--skip-build", action="store_true",
        help="No recompilar (usa el binario ya construido)"
    )
    parser.add_argument(
        "--no-opt", action="store_true",
        help="Omitir el cálculo del algoritmo óptimo (sin recompilación extra)"
    )
    parser.add_argument(
        "--programs", nargs="+", default=USER_PROGRAMS,
        help="Programas a evaluar (default: matmult sort)"
    )
    args = parser.parse_args()

    num_frames = args.frames if args.frames else 32
    print(f"\n{'='*60}")
    print(f"  Nachos – Benchmark de Reemplazo de Páginas")
    print(f"  Marcos de memoria: {num_frames}")
    print(f"  Ejecuciones por experimento: {args.runs}")
    print(f"  Programas: {', '.join(args.programs)}")
    print(f"{'='*60}\n")

    all_data: dict[str, dict] = {}

    for program in args.programs:
        print(f"\n{'─'*50}")
        print(f"  Evaluando programa: {program}")
        print(f"{'─'*50}")

        pol_data: dict[str, dict] = {}

        # ── Políticas simuladas ──────────────────────────────────────────────
        for pol_name, pol_flag in POLICIES.items():
            print(f"\n  Política: {pol_name}")
            result = benchmark(
                program=program,
                policy_name=pol_name,
                runs=args.runs,
                skip_build=args.skip_build,
                policy_flag=pol_flag,
                num_frames=num_frames,
            )
            pol_data[pol_name] = result
            pf = result.get("pageFaults", "N/A")
            dr = result.get("diskReads",  "N/A")
            dw = result.get("diskWrites", "N/A")
            print(f"    Fallos={pf}  DiskReads={dr}  DiskWrites={dw}")

        # ── Algoritmo ÓPTIMO ────────────────────────────────────────────────
        opt_result: dict = {}
        if not args.no_opt:
            print(f"\n  Política: ÓPTIMO (Bélády off-line)")
            trace = collect_trace_with_build(program, num_frames=num_frames)
            if trace:
                opt_faults = optimal_faults(trace, num_frames)
                print(f"    Fallos óptimos (OPT, {num_frames} marcos): {opt_faults}")
                opt_result["opt_faults"] = opt_faults
                opt_result["pageFaults"] = opt_faults
                opt_result["trace_length"] = len(trace)
                opt_result["unique_pages"] = len(set(trace))
            else:
                print(
                    "    ADVERTENCIA: No se pudo capturar la traza.\n"
                    "    Para habilitar TRACE_PAGES agregue en userprog/exception.cc\n"
                    "    (en el handler de page fault) la línea:\n"
                    "      #ifdef TRACE_PAGES\n"
                    "        fprintf(stderr, \"PTRACE %d\\n\", vpn);\n"
                    "      #endif\n"
                    "    y recompile."
                )
        pol_data["OPTIMO"] = opt_result

        # ── Tabla del programa ───────────────────────────────────────────────
        print_table(program, {k: v for k, v in pol_data.items() if k != "OPTIMO"},
                    opt_result)
        all_data[program] = pol_data

    # ── Resumen final ────────────────────────────────────────────────────────
    print_summary_comparison(all_data)

    # ── CSV de resultados ────────────────────────────────────────────────────
    csv_path = SCRIPT_DIR / "benchmark_results.csv"
    with open(csv_path, "w", encoding="utf-8") as f:
        # Encabezado
        metrics_keys = [k for k in STAT_PATTERNS.keys()]
        f.write("programa,politica," + ",".join(metrics_keys) + ",opt_faults\n")
        for prog, pol_data in all_data.items():
            for pol, mdata in pol_data.items():
                vals = [str(mdata.get(k, "")) for k in metrics_keys]
                opt_f = str(mdata.get("opt_faults", ""))
                f.write(f"{prog},{pol}," + ",".join(vals) + f",{opt_f}\n")

    print(f"\n  Resultados guardados en: {csv_path}")
    print()


if __name__ == "__main__":
    main()
