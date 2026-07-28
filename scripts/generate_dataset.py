#!/usr/bin/env python3
"""
Script determinista de generación de datasets para los experimentos del SGBD.
Garantiza repetibilidad usando una semilla fija (seed=42).
"""

import sys
import random

def generate_dataset(output_file, record_count=10000, seed=42):
    random.seed(seed)
    names = ["Ana", "Carlos", "Beatriz", "David", "Elena", "Fernando", "Gabriela", "Hugo", "Irene", "Juan"]
    
    with open(output_file, "w", encoding="utf-8") as f:
        f.write("CREATE TABLE users (id INT, name CHAR(30), age INT, active BOOLEAN);\n")
        for i in range(1, record_count + 1):
            name = f"{random.choice(names)}_{i}"
            age = random.randint(18, 75)
            active = "true" if random.random() > 0.5 else "false"
            f.write(f"INSERT INTO users VALUES ({i}, '{name}', {age}, {active});\n")
        f.write("CREATE INDEX idx_users_id ON users(id);\n")
    
    print(f"[OK] Generated dataset script with {record_count} records in {output_file}")

if __name__ == "__main__":
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 10000
    out = sys.argv[2] if len(sys.argv) > 2 else "data/dataset.sql"
    generate_dataset(out, count)
