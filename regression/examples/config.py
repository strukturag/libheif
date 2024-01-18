import json
import os
import subprocess

class Config:
    def __init__(self):
        with open("version.json") as version_data:
            self.version = json.load(version_data)
        print(self.version)

        with open("test_data.json") as test_data:
            self.test_config = json.load(test_data)
        print(self.test_config)

        if not os.path.exists(self.test_config["conformance_test_data"]):
            subprocess.run(["git", "clone", "https://github.com/nokiatech/heif_conformance", self.test_config["conformance_test_data"]])

    def heif_version(self):
        return self.version["version_str"]
    
    def conformance_file_path(self, filename):
        return self.test_config["conformance_test_data"] + "/conformance_files/" + filename
    
    def getExpectedFileInfo(self, filename):
        return self.test_config["static_test_data"] + "/conformance_files_expected_info/" + filename + ".txt"
    
    def getExpectedFileDump(self, filename):
        return self.test_config["static_test_data"] + "/conformance_files_expected_dump/" + filename + ".dump"