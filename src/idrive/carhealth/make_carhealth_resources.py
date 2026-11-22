"""Build the NbtCarHealth plugin resources (XML, manifest, text/image zips) into <output-dir>. Never installs anything.
Usage: python make_carhealth_resources.py <output-dir>   (libNbtCarHealth.so is built by scripts/build-open.sh)"""
from pathlib import Path
import shutil
import sys
import zipfile
import xml.etree.ElementTree as ET

source = Path(__file__).resolve().parent
output = Path(sys.argv[1]).resolve()
output.mkdir(parents=True, exist_ok=True)
for name in ('NbtCarHealth.xml', 'NbtCarHealth.manifest'):
    ET.parse(source / name)
    shutil.copyfile(source / name, output / name)
# the rows themselves are live text from nbt_health; only the entry/page title is a text resource
GERMAN = {1: 'Fahrzeugzustand'}
ENGLISH = {1: 'Car health'}
languages = ['de', 'en-UK', 'en-US', 'es-US', 'es-ES', 'fr-FR', 'fr-CA', 'it', 'pt', 'ru']
with zipfile.ZipFile(output / 'NbtCarHealth_ALL_Texts.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for language in languages:
        texts = GERMAN if language == 'de' else ENGLISH
        body = '100000=' + language + '\n' + ''.join('%d=%s\n' % (k, v) for k, v in sorted(texts.items()))
        archive.writestr(language + '.txt', body)
# empty, as in NbtAndroidAuto (history: image resources once made an entry disappear)
with zipfile.ZipFile(output / 'NbtCarHealth_ALL_Images.zip', 'w'):
    pass
print('NbtCarHealth resources created:', output)
