import re, pathlib

# ---------- source renames ----------
p = pathlib.Path('src/main.cpp')
s = p.read_text(encoding='utf-8')
s = s.replace('SetWindowTextW(app.hwnd, L"MD Reader");', 'SetWindowTextW(app.hwnd, L"MdReader");')
s = s.replace('const wchar_t* kProgId = L"MDReader.md";', 'const wchar_t* kProgId = L"MdReader.md";')
s = s.replace('L"Software\\\\Classes\\\\MDReader.md"', 'L"Software\\\\Classes\\\\MdReader.md"')
s = s.replace('L"Software\\\\Classes\\\\MDReader.md\\\\DefaultIcon"', 'L"Software\\\\Classes\\\\MdReader.md\\\\DefaultIcon"')
s = s.replace('L"Software\\\\Classes\\\\MDReader.md\\\\shell\\\\open\\\\command"', 'L"Software\\\\Classes\\\\MdReader.md\\\\shell\\\\open\\\\command"')
s = s.replace('Applications\\\\mdreader.exe', 'Applications\\\\MdReader.exe')
s = s.replace('const wchar_t* friendly = L"MD Reader";', 'const wchar_t* friendly = L"MdReader";')
s = s.replace('const wchar_t* appName = L"mdreader.exe";', 'const wchar_t* appName = L"MdReader.exe";')
s = s.replace('const wchar_t* exeName = L"mdreader.exe";', 'const wchar_t* exeName = L"MdReader.exe";')
s = s.replace('L"MD Reader"', 'L"MdReader"')
s = s.replace('L"MDReaderWindow"', 'L"MdReaderWindow"')
p.write_text(s, encoding='utf-8')

for f in ('src/chrome.cpp', 'src/main.cpp', 'src/graphics.cpp'):
    p = pathlib.Path(f)
    s = p.read_text(encoding='utf-8')
    s = s.replace('L"MD Reader"', 'L"MdReader"')
    p.write_text(s, encoding='utf-8')

# ---------- build / cmake ----------
p = pathlib.Path('build.sh')
s = p.read_text(encoding='utf-8')
s = s.replace('-o build/mdreader.exe', '-o build/MdReader.exe')
s = s.replace('ls -l build/mdreader.exe', 'ls -l build/MdReader.exe')
p.write_text(s, encoding='utf-8')

p = pathlib.Path('CMakeLists.txt')
s = p.read_text(encoding='utf-8')
s = s.replace('add_executable(mdreader WIN32', 'add_executable(MdReader WIN32')
s = s.replace('target_link_libraries(mdreader PRIVATE', 'target_link_libraries(MdReader PRIVATE')
s = s.replace('set_property(TARGET mdreader PROPERTY', 'set_property(TARGET MdReader PROPERTY')
s = s.replace('target_compile_options(mdreader PRIVATE', 'target_compile_options(MdReader PRIVATE')
p.write_text(s, encoding='utf-8')

# ---------- version resource ----------
p = pathlib.Path('res/mdreader.rc')
s = p.read_text(encoding='utf-8')
s = s.replace('VALUE "FileDescription", "MD Reader"', 'VALUE "FileDescription", "MdReader"')
s = s.replace('VALUE "ProductName", "MD Reader"', 'VALUE "ProductName", "MdReader"')
s = s.replace('VALUE "InternalName", "mdreader"', 'VALUE "InternalName", "MdReader"')
s = s.replace('VALUE "OriginalFilename", "mdreader.exe"', 'VALUE "OriginalFilename", "MdReader.exe"')
s = s.replace('FILEVERSION 1,0,0,0', 'FILEVERSION 1,0,1,0')
s = s.replace('PRODUCTVERSION 1,0,0,0', 'PRODUCTVERSION 1,0,1,0')
s = s.replace('VALUE "FileVersion", "1.0.0.0"', 'VALUE "FileVersion", "1.0.1.0"')
s = s.replace('VALUE "ProductVersion", "1.0.0.0"', 'VALUE "ProductVersion", "1.0.1.0"')
p.write_text(s, encoding='utf-8')

# ---------- gitignore stays ----------
print('renamed')
