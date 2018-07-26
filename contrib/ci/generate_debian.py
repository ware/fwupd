#!/usr/bin/python3
#
# Copyright (C) 2017 Dell Inc.
#
# SPDX-License-Identifier: LGPL-2.1+
#
import os
import sys
import xml.etree.ElementTree as etree

def parse_control_dependencies(requested_type):
    TARGET=os.getenv('OS')
    deps = []
    dep = ''

    if TARGET == '':
        print("Missing OS environment variable")
        sys.exit(1)
    OS = TARGET
    SUBOS = ''
    if TARGET:
        split = TARGET.split('-')
        if len(split) >= 2:
            OS = split[0]
            SUBOS = split[1]
    else:
        import lsb_release
        OS = lsb_release.get_distro_information()['ID'].lower()
        import platform
        SUBOS = platform.machine()

    tree = etree.parse(os.path.join(os.path.dirname (sys.argv[0]), "dependencies.xml"))
    root = tree.getroot()
    for child in root:
        if not "type" in child.attrib or not "id" in child.attrib:
            continue
        for distro in child:
            if not "id" in distro.attrib:
                continue
            if distro.attrib["id"] != OS:
                continue
            control = distro.find("control")
            if control is None:
                continue
            packages = distro.findall("package")
            for package in packages:
                if SUBOS:
                    if not 'variant' in package.attrib:
                        continue
                    if package.attrib['variant'] != SUBOS:
                        continue
                if package.text:
                    dep = package.text
                else:
                    dep = child.attrib["id"]
                if child.attrib["type"] == requested_type and dep:
                    version = control.find('version')
                    if version is not None:
                        dep = "%s %s" % (dep, version.text)
                    inclusions = control.findall('inclusive')
                    if inclusions:
                        for i in range(0, len(inclusions)):
                            prefix = ''
                            suffix = ' '
                            if i == 0:
                                prefix = " ["
                            if i == len(inclusions) - 1:
                                suffix = "]"
                            dep = "%s%s%s%s" % (dep, prefix, inclusions[i].text, suffix)
                    exclusions = control.findall('exclusive')
                    if exclusions:
                        for i in range(0, len(exclusions)):
                            prefix = '!'
                            suffix = ' '
                            if i == 0:
                                prefix = " [!"
                            if i == len(exclusions) - 1:
                                suffix = "]"
                            dep = "%s%s%s%s" % (dep, prefix, exclusions[i].text, suffix)
                    deps.append(dep)
    return deps

def update_debian_control(target):
    control_in = os.path.join(target, 'control.in')
    control_out = os.path.join(target, 'control')

    if not os.path.exists(control_in):
        print("Missing file %s" % control_in)
        sys.exit(1)

    with open(control_in, 'r') as rfd:
        lines = rfd.readlines()

    deps = parse_control_dependencies("build")
    deps.sort()
    with open(control_out, 'w') as wfd:
        for line in lines:
            if line.startswith("Build-Depends: %%%DYNAMIC%%%"):
                wfd.write("Build-Depends:\n")
                for i in range(0, len(deps)):
                    wfd.write("\t%s,\n" % deps[i])
            else:
                wfd.write(line)

def update_debian_copyright (directory):
    copyright_in = os.path.join(directory, 'copyright.in')
    copyright_out = os.path.join(directory, 'copyright')

    if not os.path.exists(copyright_in):
        print("Missing file %s" % copyright_in)
        sys.exit(1)

    license_matches = {}
    copyright_matches = {}
    for root, dirs, files in os.walk('.'):
        for file in files:
            license = ''
            copyright = []
            target = os.path.join (root, file)
            try:
                with open(target, 'r') as rfd:
                    lines = rfd.readlines()
            except UnicodeDecodeError:
                continue
            for line in lines:
                if 'SPDX-License-Identifier' in line:
                    license = line.split (':')[1].strip()
                if 'Copyright' in line:
                    parts = line.partition ('Copyright')
                    copyright += ["%s %s" % (parts[1], parts[2].strip())]
                if license and copyright:
                    break
            if license and copyright:
                license_matches [target] = license
                copyright_matches [target] = copyright
                license = ''

    with open(copyright_in, 'r') as rfd:
        lines = rfd.readlines()

    with open(copyright_out, 'w') as wfd:
        for line in lines:
            if line.startswith("%%%DYNAMIC%%%"):
                for i in license_matches:
                    wfd.write("Files: %s\n" % i)
                    for holder in copyright_matches[i]:
                        wfd.write("Copyright: %s\n" % holder)
                    wfd.write("License: %s\n" % license_matches[i])
                    wfd.write("\n")
            else:
                wfd.write(line)

directory = os.path.join (os.getcwd(), 'debian')
update_debian_control(directory)
update_debian_copyright(directory)
