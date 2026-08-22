#!/usr/bin/env python3
"""计算 RabbitMQ definitions 用 password_hash (salt 4字节 + sha256(salt+pwd), hex)

用法:
  python3 scripts/calc_mq_hash.py [密码]
  默认密码: mes_dev_pwd

输出:
  HASH=<salt><sha256_hex>
  可直接填入 deploy/mq/topology.json 的 password_hash 字段。
"""
import sys
import os
import hashlib
import secrets

def calc_hash(pwd: str) -> str:
    # RabbitMQ hashing: 4-byte random salt + SHA-256(salt + password), hex encoded
    salt = secrets.token_bytes(4)
    digest = hashlib.sha256(salt + pwd.encode("utf-8")).hexdigest()
    return salt.hex() + digest

def main():
    pwd = sys.argv[1] if len(sys.argv) > 1 else "mes_dev_pwd"
    h = calc_hash(pwd)
    print(f"HASH={h}")

if __name__ == "__main__":
    main()
