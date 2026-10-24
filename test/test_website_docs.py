# Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
"""Regression tests for documentation input and output boundaries."""

from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from scripts.build_website_docs import (
    build,
    contained_path,
    diagram_parts,
    diagrams,
    write_site_text,
)


class DocumentationTests(unittest.TestCase):
    def test_flowchart_labels_and_chained_edges(self):
        nodes, edges = diagram_parts(
            "flowchart LR\n A[Choose a team] --> B[Play] --> C[Save]\n A --> C"
        )
        self.assertEqual(nodes, {"A": "Choose a team", "B": "Play", "C": "Save"})
        self.assertEqual(edges, [("A", "B"), ("B", "C"), ("A", "C")])

    def test_unclosed_labels_do_not_create_a_diagram(self):
        text = "```mermaid\n" + "unclosed" * 100_000 + "[\n```"
        self.assertEqual(diagrams(text), (text, {}))

    def test_paths_must_stay_inside_the_checkout(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertEqual(contained_path(root, "docs/guide.html"), root / "docs/guide.html")
            for route in ("../outside.html", str(root / "absolute.html"), "."):
                with self.subTest(route=route), self.assertRaises(ValueError):
                    contained_path(root, route)

    def test_text_writer_revalidates_route_at_write_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            site = base / "site"
            site.mkdir()
            write_site_text(site, "docs/index.html", "safe")
            self.assertEqual((site / "docs/index.html").read_text(), "safe")
            with self.assertRaises(ValueError):
                write_site_text(site, "../outside.html", "unsafe")
            self.assertFalse((base / "outside.html").exists())

    def test_symlink_escape_is_rejected_before_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source, site, outside = (base / name for name in ("source", "site", "outside"))
            for path in (source, site, outside):
                path.mkdir()
            (source / "README.md").write_text("# Test\n")
            (source / "LICENSE").write_text("test licence")
            (source / "guide.md").write_text("# Guide\n")
            (site / "docs").symlink_to(outside, target_is_directory=True)
            guides = [("guide.md", "docs/guide.html", "Play", "Guide", "Test guide")]
            with patch("scripts.build_website_docs.GUIDES", guides), self.assertRaises(ValueError):
                build(source, site)
            self.assertEqual(list(outside.iterdir()), [])
            self.assertFalse((site / "LICENSE").exists())

    def test_overlapping_checkouts_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for source, site in ((root, root), (root, root / "site"), (root / "source", root)):
                with self.subTest(source=source, site=site), self.assertRaises(ValueError):
                    build(source, site)

    def test_readme_title_is_content_and_cannot_choose_an_output_path(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source, site = base / "source", base / "site"
            source.mkdir()
            (source / "README.md").write_text("# ../../escape\n")
            (source / "LICENSE").write_text("test licence")
            (source / "guide.md").write_text("# Guide\n")
            guides = [("guide.md", "docs/guide.html", "Play", "Guide", "Test guide")]
            with patch("scripts.build_website_docs.GUIDES", guides):
                build(source, site)
            self.assertTrue((site / "docs/guide.html").is_file())
            self.assertTrue((site / "docs/index.html").is_file())
            self.assertIn("../../escape", (site / "docs/guide.html").read_text())
            self.assertFalse((base / "escape").exists())


if __name__ == "__main__":
    unittest.main()
