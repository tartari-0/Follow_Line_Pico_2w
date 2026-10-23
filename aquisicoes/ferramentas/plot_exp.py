#!/usr/bin/env python3
"""Plota os ensaios gravados pelo acquire.py.

    python3 aquisicoes/ferramentas/plot_exp.py aquisicoes/dados/<pasta>/arquivo.csv
    python3 aquisicoes/ferramentas/plot_exp.py aquisicoes/dados/<pasta>/*.csv --salvar
    python3 aquisicoes/ferramentas/plot_exp.py a.csv b.csv --sobrepor   # compara ensaios

--salvar grava o PNG ao lado de cada CSV em vez de abrir a janela.
"""

import argparse
import json
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

MOTORS = [("1", "M1 (esq)"), ("2", "M2 (dir)")]
RAD_S_TO_RPM = 60.0 / (2.0 * np.pi)


def load(path):
    df = pd.read_csv(path)
    meta_path = Path(path).with_suffix(".json")
    meta = json.loads(meta_path.read_text()) if meta_path.exists() else {}
    return df, meta


def active_motors(df):
    """Motores que receberam algum PWM no ensaio."""
    active = [(m, label) for m, label in MOTORS if (df[f"pwm{m}"] != 0).any()]
    return active or MOTORS


def title_of(path, meta):
    title = f"exp {meta.get('ensaio', Path(path).stem)}"
    if meta.get("nota"):
        title += f" — {meta['nota']}"
    if meta.get("amostras_faltando"):
        title += f"  [{meta['amostras_faltando']} amostras faltando]"
    return title


def plot_single(path):
    df, meta = load(path)
    motors = active_motors(df)
    t = df["t_s"]

    fig, axes = plt.subplots(4, 1, sharex=True, figsize=(10, 9))
    fig.suptitle(title_of(path, meta))

    for m, label in motors:
        axes[0].plot(t, df[f"pwm{m}"], label=label)
        axes[1].plot(t, df[f"speed{m}"], label=label)
        axes[2].plot(t, df[f"enc{m}"] - df[f"enc{m}"].iloc[0], label=label)
    axes[3].plot(t, df["vbat"], color="tab:red")

    axes[0].set_ylabel("PWM (%)")
    axes[1].set_ylabel("velocidade (rad/s)")
    axes[2].set_ylabel("pulsos (desde o início)")
    axes[3].set_ylabel("bateria (V)")
    axes[3].set_xlabel("tempo (s)")

    # Eixo secundário em RPM para a velocidade
    rpm = axes[1].secondary_yaxis("right", functions=(lambda w: w * RAD_S_TO_RPM, lambda r: r / RAD_S_TO_RPM))
    rpm.set_ylabel("RPM")

    for ax in axes[:3]:
        ax.legend(loc="upper right")
    for ax in axes:
        ax.grid(True, alpha=0.3)
    fig.tight_layout()
    return fig


def plot_overlay(paths):
    fig, axes = plt.subplots(2, 1, sharex=True, figsize=(10, 6))
    fig.suptitle("comparação de ensaios")
    for path in paths:
        df, meta = load(path)
        name = Path(path).stem
        for m, label in active_motors(df):
            axes[0].plot(df["t_s"], df[f"pwm{m}"], label=f"{name} {label}")
            axes[1].plot(df["t_s"], df[f"speed{m}"], label=f"{name} {label}")
    axes[0].set_ylabel("PWM (%)")
    axes[1].set_ylabel("velocidade (rad/s)")
    axes[1].set_xlabel("tempo (s)")
    for ax in axes:
        ax.grid(True, alpha=0.3)
    axes[1].legend(fontsize="small")
    fig.tight_layout()
    return fig


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("arquivos", nargs="+", type=Path)
    parser.add_argument("--salvar", action="store_true", help="salva PNG em vez de abrir a janela")
    parser.add_argument("--sobrepor", action="store_true", help="sobrepõe PWM e velocidade de todos os arquivos")
    args = parser.parse_args()

    if args.sobrepor:
        fig = plot_overlay(args.arquivos)
        if args.salvar:
            out = args.arquivos[0].with_name("comparacao.png")
            fig.savefig(out, dpi=150)
            print(f"salvo: {out}")
    else:
        for path in args.arquivos:
            fig = plot_single(path)
            if args.salvar:
                out = path.with_suffix(".png")
                fig.savefig(out, dpi=150)
                plt.close(fig)
                print(f"salvo: {out}")

    if not args.salvar:
        plt.show()


if __name__ == "__main__":
    main()
