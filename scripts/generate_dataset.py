#!/usr/bin/env python3
"""Genera un script SQL determinista para demostraciones del mini-SGBD."""

from __future__ import annotations

import argparse
import random
from pathlib import Path


def generate_dataset(
    output_file: Path,
    record_count: int,
    seed: int,
) -> None:
    if record_count <= 0:
        raise ValueError("record_count must be greater than zero")

    random_generator = random.Random(seed)
    names = [
        "Ana",
        "Carlos",
        "Beatriz",
        "David",
        "Elena",
        "Fernando",
        "Gabriela",
        "Hugo",
        "Irene",
        "Juan",
    ]

    output_file.parent.mkdir(parents=True, exist_ok=True)

    with output_file.open("w", encoding="utf-8", newline="\n") as output:
        output.write(
            "CREATE TABLE users "
            "(id INT, name VARCHAR(30), age INT, active BOOLEAN);\n"
        )

        for identifier in range(1, record_count + 1):
            name = f"{random_generator.choice(names)}_{identifier}"
            age = random_generator.randint(18, 75)
            active = "true" if random_generator.random() >= 0.5 else "false"

            output.write(
                "INSERT INTO users VALUES "
                f"({identifier}, '{name}', {age}, {active});\n"
            )

        output.write("CREATE INDEX idx_users_id ON users(id);\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("record_count", nargs="?", type=int, default=10_000)
    parser.add_argument(
        "output_file",
        nargs="?",
        type=Path,
        default=Path("data/dataset_10k.sql"),
    )
    parser.add_argument("--seed", type=int, default=42)
    arguments = parser.parse_args()

    generate_dataset(
        arguments.output_file,
        arguments.record_count,
        arguments.seed,
    )

    print(
        f"[OK] Generated {arguments.record_count} deterministic records "
        f"in {arguments.output_file} (seed={arguments.seed})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
