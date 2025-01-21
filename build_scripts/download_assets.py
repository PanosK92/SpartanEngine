#Copyright(c) 2016-2025 Panos Karabelas
#
#Permission is hereby granted, free of charge, to any person obtaining a copy
#of this software and associated documentation files (the "Software"), to deal
#in the Software without restriction, including without limitation the rights
#to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
#copies of the Software, and to permit persons to whom the Software is furnished
#to do so, subject to the following conditions :
#
#The above copyright notice and this permission notice shall be included in
#all copies or substantial portions of the Software.
#
#THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
#FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
#COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import file_utilities

file_url           = 'https://www.dropbox.com/scl/fi/pqcw4gqemb7fo5afstn89/assets.7z?rlkey=wrn1ludhkjz7trgxfrkjsch2k&st=52fsu574&dl=1'
file_destination   = 'project/assets.7z'
file_expected_hash = 'bebddf7a39e479e0ad9623a9251a465be9fb68a1a0a17ac2212f25de03255b21'

def main():
    file_utilities.download_file(file_url, file_destination, file_expected_hash)
    file_utilities.extract_archive(file_destination, "project/")

if __name__ == "__main__":
    main()
