#!/usr/bin/env python
# This script uploads binaries from the build server to Google Code.

import datetime, os, re, shutil, sys, zipfile
import googlecode_upload
from subprocess import call

server = "callisto.local"
project = "ampl"

summaries = {
  "amplgsl": "AMPL bindings for the GNU Scientific Library",
  "ampltabl": "ODBC table handler",
  "gecode": "Gecode solver",
  "cbc": "COIN-OR CBC solver"
}

labels = {
  "linux32": "OpSys-Linux",
  "linux64": "OpSys-Linux",
  "macosx": "OpSys-OSX",
  "win32": "OpSys-Windows",
  "win64": "OpSys-Windows"
}

def rmtree(dir):
  if os.path.exists(dir):
    shutil.rmtree(dir)

def upload(filename, summary, labels):
  print("Uploading {}".format(filename))
  from netrc import netrc
  authenticators = netrc().authenticators("code.google.com")
  username = authenticators[0]
  password = authenticators[2]
  status, reason, url = googlecode_upload.upload(
    filename, project, username, password, summary, labels)
  if not url:
    print('Google Code upload error: {} ({})'.format(reason, status))

dir = "ampl-open"
gecode_version = None
for platform in reversed(["linux32", "linux64", "macosx", "win32", "win64"]):
  rmtree(dir)
  print("Downloading binaries for {}:".format(platform))
  call("scp -r {}:/var/lib/buildbot/upload/{} {}".format(server, platform, dir),
       shell=True)
  # Get versions.
  versions_filename = dir + "/versions"
  versions = {}
  if os.path.exists(versions_filename):
    with open(versions_filename) as f:
      for line in f:
        items = line.rstrip().split(' ')
        if len(items) < 2:
          continue
        version = items[1]
        m = re.match(r'.*driver\(([0-9]+)\)', line)
        if m:
          version += '-' + m.group(1)
        versions[items[0].lower()] = version
  date = datetime.datetime.today()
  date = "{}{:02}{:02}".format(date.year, date.month, date.day)
  dirlen = len(dir) + 1
  jacop_version = versions['jacop']
  jacop_version = jacop_version[:jacop_version.rfind('-')]
  extra_files = {
    'jacop': ['ampljacop.jar', 'JaCoP-' + jacop_version + '.jar']
  }
  file_package = {}
  for package, files in extra_files.iteritems():
    for f in files:
      file_package[f] = package

  # Upload individual files.
  paths = []
  for base, dirs, files in os.walk(dir):
    for file in files:
      path = os.path.join(base, file)
      name = path[dirlen:]
      if name == "versions" or name in file_package:
        continue
      basename = os.path.splitext(name)[0]
      suffix = versions.get(basename, date)
      archive_name = "{}-{}-{}.zip".format(basename, suffix, platform)
      with zipfile.ZipFile(archive_name, 'w', zipfile.ZIP_DEFLATED) as zip:
        zip.write(path, name)
        files = extra_files.get(basename)
        if files:
          for f in files:
            zip.write(os.path.join(base, f), f)

      upload(archive_name, summaries[basename], [labels[platform]])
      os.remove(archive_name)
      paths.append(path)
  # Upload all in one archive.
  archive_name = "ampl-open-{}-{}.zip".format(date, platform)
  with zipfile.ZipFile(archive_name, 'w', zipfile.ZIP_DEFLATED) as zip:
    for path in paths:
      zip.write(path, path[dirlen:])
    zip.write("LICENSE", "LICENSE")
  upload(archive_name, "Open-source AMPL solvers and libraries", None)
  os.remove(archive_name)

def update_redir_page(name):
  filename = "ampl.wiki/{}.html".format(name)
  with open(filename, 'r') as f:
    content = f.read()
  version = re.sub(r".*-()", r"\1", versions.get(name))
  print 'changing version: {}, {}'.format(versions.get(name), version)
  print content
  content = re.sub(r"q={}\+(\d+)".format(name), "q={}+{}".format(name, version), content)
  print content
  with open(filename, 'w') as f:
    f.write(content)

# Update the page that redirects to the downloads for the most recent version of Gecode.
rmtree("ampl.wiki")
call(["git", "clone", "https://code.google.com/p/ampl.wiki/"])
update_redir_page('ampltabl')
update_redir_page('gecode')
call(["git", "commit", "-a", "-m", "update versions"], cwd="ampl.wiki/")
call(["git", "push"], cwd="ampl.wiki/")
