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

file_url           = 'https://www.dropbox.com/scl/fi/jdfao80lt0iogwrus5fam/assets.7z?rlkey=i124r0jdduw5wvqa9u77a3k2a&st=wntdsfcj&dl=1'
file_destination   = 'project/assets.7z'
file_expected_hash = '559be6e005a441187a3be9cf939826ca3434eb17e9fc115ec5a81f6b1bda903b'

def main():
    file_utilities.download_file(file_url, file_destination, file_expected_hash)
    file_utilities.extract_archive(file_destination, "project/")

if __name__ == "__main__":
    main()
