#!/usr/bin/env python3
import os
import re
import datetime
import sys
import argparse

def main():
    parser = argparse.ArgumentParser(description="Update copyright headers.")
    parser.add_argument('--check', action='store_true', help="Check if any files need updating, exit 1 if they do.")
    args = parser.parse_args()

    current_year = datetime.datetime.now().year

    directories = ['src', 'test', 'benchmarks']
    files_to_update = []

    for d in directories:
        for root, _, files in os.walk(d):
            for file in files:
                if file.endswith(('.cpp', '.h')):
                    files_to_update.append(os.path.join(root, file))

    needs_update = False

    for filepath in files_to_update:
        with open(filepath, 'r') as f:
            content = f.read()

        expected_copyright = f"//  Copyright (c) 2025 - {current_year} Flavio Milinanni. All Rights Reserved."

        # Regex to find the copyright line and completely remove any old Commit's Author lines we added
        pattern = re.compile(
            r'//\s*Copyright \(c\)\s*[0-9]{4}(?:\s*-\s*[0-9]{4})?\s*Flavio Milinanni\.\s*All Rights Reserved\.\n'
            r'(?://\s*(?:Commit\'s Author\s*:|commit\'s author\s*:|Author\s*:).*\n)?'
        )

        replacement = f"{expected_copyright}\n"

        if pattern.search(content):
            new_content = pattern.sub(replacement, content)
            if new_content != content:
                if args.check:
                    print(f"[Check Failed] File needs copyright update: {filepath}")
                    needs_update = True
                else:
                    with open(filepath, 'w') as f:
                        f.write(new_content)
                    print(f"Updated {filepath}")
        else:
            header = (
                "// -----------------------------------------------------------------------------\n"
                "//  Football Management Project\n"
                f"{expected_copyright}\n"
                "//\n"
                "//  This file is part of the Football Management Project.\n"
                "//  See the LICENSE file in the project root.\n"
                "// -----------------------------------------------------------------------------\n\n"
            )
            if args.check:
                print(f"[Check Failed] File missing copyright header: {filepath}")
                needs_update = True
            else:
                with open(filepath, 'w') as f:
                    f.write(header + content)
                print(f"Added header to {filepath}")

    if args.check and needs_update:
        sys.exit(1)
    elif args.check:
        print("All files have up-to-date copyright headers.")
        sys.exit(0)

if __name__ == '__main__':
    main()
