from __future__ import annotations

import argparse
from pathlib import Path

from agri_gateway.client import GatewayClient, log
from agri_gateway.config import GatewayConfig
from agri_gateway.providers import (
    DeepSeekProvider,
    MockProvider,
    ProviderError,
    load_api_key,
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="AgricultureSys STM32/DeepSeek gateway")
    parser.add_argument("device_ip", help="STA IP shown on the STM32 TFT")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--provider", choices=("deepseek", "mock"), default="deepseek")
    parser.add_argument("--model", default="deepseek-v4-flash")
    parser.add_argument("--api-timeout", type=float, default=45.0)
    parser.add_argument(
        "--key-file",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "myDeepseekApiKey.md",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    config = GatewayConfig(
        device_ip=args.device_ip,
        port=args.port,
        provider=args.provider,
        model=args.model,
        api_timeout_seconds=args.api_timeout,
        key_file=args.key_file,
    )
    config.validate()

    if config.provider == "mock":
        provider = MockProvider()
        log("CONFIG", "provider=mock (no API call)")
    else:
        try:
            api_key = load_api_key(config.key_file)
        except ProviderError as exc:
            log("CONFIG", f"cannot load DeepSeek key: {exc.code}")
            return 2
        provider = DeepSeekProvider(api_key, config.model, config.api_timeout_seconds)
        log("CONFIG", f"provider=deepseek model={config.model}; key loaded securely")

    client = GatewayClient(config, provider)
    try:
        client.run_forever()
    except KeyboardInterrupt:
        log("GATEWAY", "stopped by user")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
