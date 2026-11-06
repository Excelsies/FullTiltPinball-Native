"""Write RetroPie 4.8's es_systems.cfg reordered to match an old custom ordering.

usage: reorder_systems.py <new 4.8 es_systems.cfg> <old custom es_systems.cfg> <output path>
Systems in the old list come first, in its order; systems only in 4.8 follow in 4.8's order.
"""
import sys
import xml.etree.ElementTree as ET


def main(new_path: str, old_path: str, out_path: str) -> None:
    new_root = ET.parse(new_path).getroot()
    old_order = [s.findtext("name") for s in ET.parse(old_path).getroot().findall("system")]
    systems = new_root.findall("system")
    # Names can repeat (stock RetroPie lists "videopac" twice), so keep every entry per name.
    by_name: dict[str, list] = {}
    for s in systems:
        by_name.setdefault(s.findtext("name"), []).append(s)

    ordered, seen = [], set()
    for name in old_order + [s.findtext("name") for s in systems]:
        if name in by_name and name not in seen:
            ordered.extend(by_name[name])
            seen.add(name)
    assert len(ordered) == len(systems), "reorder lost or duplicated a system"

    for s in systems:
        new_root.remove(s)
    new_root.extend(ordered)
    ET.ElementTree(new_root).write(out_path, encoding="utf-8", xml_declaration=True)
    dropped = [n for n in dict.fromkeys(old_order) if n not in by_name]
    print(f"{len(ordered)} systems written; first 10: {[s.findtext('name') for s in ordered[:10]]}")
    print(f"old systems not in 4.8 (ignored): {dropped}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])
