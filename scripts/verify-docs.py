from pathlib import Path
import re


def main() -> None:
    root = Path("docs")
    required = [
        root / "index.html",
        root / "ecosystem.md",
        root / "platforms.md",
    ]

    for path in required:
        if not path.exists():
            raise SystemExit(f"Missing docs file: {path}")

    html = (root / "index.html").read_text(encoding="utf-8")
    expected_text = [
        "hotc233-unity",
        "Assets/neko233/hotc233-unity",
        "ecosystem.md",
        "platforms.md",
    ]

    for text in expected_text:
        if text not in html:
            raise SystemExit(f"docs/index.html missing expected text: {text}")

    for href in re.findall(r'href="([^"]+)"', html):
        if href.startswith(("#", "http://", "https://")):
            continue
        target = root / href
        if not target.exists():
            raise SystemExit(f"Missing docs link target: {href}")

    ecosystem = (root / "ecosystem.md").read_text(encoding="utf-8")
    platforms = (root / "platforms.md").read_text(encoding="utf-8")

    if "GitHub Pages" not in ecosystem:
        raise SystemExit("docs/ecosystem.md must mention GitHub Pages")
    if "WebGL 2" not in platforms:
        raise SystemExit("docs/platforms.md must mention WebGL 2")


if __name__ == "__main__":
    main()
