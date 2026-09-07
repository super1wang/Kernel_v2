"""D0.06-a 锁与获取边界的先行反例；不依赖网络。"""
from copy import deepcopy
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"tools/dependencies"))
from prepare import LockError, load_lock, validate_lock, select_dependencies, materialize, archive_tree

class LockContracts(unittest.TestCase):
    def setUp(self): self.lock=load_lock(ROOT/"dependencies.lock")
    def test_complete_default_table(self):
        self.assertEqual(set(self.lock["dependencies"]),{"expected","jsoncons","thread_pool","asio","immer","spdlog","fmt","sqlite","toml11","cli11","catch2","benchmark"})
        validate_lock(self.lock)
    def test_reject_floating_revision(self):
        for field,value in (("commit","main"),("archive_sha256","0"),("source_url","https://example.com/latest.tar.gz")):
            lock=deepcopy(self.lock);lock["dependencies"]["expected"][field]=value
            with self.assertRaises(LockError):validate_lock(lock)
    def test_reject_incompatible_toolchain(self):
        for field,value in (("architecture","Win32"),("cxx_standard",17),("msvc_runtime","MultiThreaded")):
            lock=deepcopy(self.lock);lock["toolchain"][field]=value
            with self.assertRaises(LockError):validate_lock(lock)
    def test_reject_missing_license(self):
        lock=deepcopy(self.lock);lock["dependencies"]["expected"].pop("license")
        with self.assertRaises(LockError):validate_lock(lock)
    def test_reject_sqlite_without_fix(self):
        for version in ("3.51.2","3.52.0"):
            lock=deepcopy(self.lock);lock["dependencies"]["sqlite"]["version"]=version
            with self.assertRaises(LockError):validate_lock(lock)
    def test_reject_unsafe_compile_options(self):
        lock=deepcopy(self.lock);lock["dependencies"]["sqlite"]["compile_definitions"]["SQLITE_THREADSAFE"]="0"
        with self.assertRaises(LockError):validate_lock(lock)
    def test_component_selection(self):
        embedded=select_dependencies(self.lock,["Embedded"])
        self.assertEqual(embedded,{"expected","thread_pool","spdlog","fmt"})
        self.assertTrue({"sqlite","asio","immer"}.isdisjoint(embedded))
        self.assertEqual(select_dependencies(self.lock,["Foundation"]),{"expected"})
        self.assertTrue({"sqlite","expected"}<=select_dependencies(self.lock,["SQLite"]))
        self.assertIn("immer",select_dependencies(self.lock,["State"]))
    def test_unknown_component_rejected(self):
        with self.assertRaises(LockError):select_dependencies(self.lock,["Statte"])
    def test_unselected_never_downloaded(self):
        with tempfile.TemporaryDirectory() as temp,patch("prepare.download",side_effect=AssertionError("unexpected network")):
            report=materialize(self.lock,[],Path(temp),offline=True)
            self.assertEqual(report["selected"],[])
            self.assertEqual(list(Path(temp).iterdir()),[])
    def test_offline_missing_refused(self):
        with tempfile.TemporaryDirectory() as temp,patch("prepare.download",side_effect=AssertionError("unexpected network")):
            with self.assertRaises(LockError):materialize(self.lock,["Foundation"],Path(temp),offline=True)
    def test_hash_mismatch_refused(self):
        with tempfile.TemporaryDirectory() as temp,patch("prepare.download",return_value=b"wrong archive"):
            with self.assertRaises(LockError):materialize(self.lock,["Foundation"],Path(temp),offline=False)
    def test_archive_traversal_refused(self):
        for name in ("root/../../escape.txt","/absolute.txt","root/C:/escape.txt"):
            stream=io.BytesIO()
            with tarfile.open(fileobj=stream,mode="w:gz") as archive:
                item=tarfile.TarInfo(name);item.size=1;archive.addfile(item,io.BytesIO(b"x"))
            with self.assertRaises(LockError):archive_tree(stream.getvalue(),"tar.gz")
    def test_internal_document_link_materialized(self):
        stream=io.BytesIO()
        with tarfile.open(fileobj=stream,mode="w:gz") as archive:
            item=tarfile.TarInfo("root/extra/readme");item.size=1;archive.addfile(item,io.BytesIO(b"x"))
            link=tarfile.TarInfo("root/doc/readme");link.type=tarfile.SYMTYPE;link.linkname="../extra/readme";archive.addfile(link)
        self.assertEqual(archive_tree(stream.getvalue(),"tar.gz")["doc/readme"],b"x")
    def test_archive_symlink_refused(self):
        stream=io.BytesIO()
        with tarfile.open(fileobj=stream,mode="w:gz") as archive:
            item=tarfile.TarInfo("root/link");item.type=tarfile.SYMTYPE;item.linkname="outside";archive.addfile(item)
        with self.assertRaises(LockError):archive_tree(stream.getvalue(),"tar.gz")

if __name__=="__main__":unittest.main(verbosity=2)
