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

    # find license on all files
    license_matches = {}
    copyright_matches = {}
    for root, dirs, files in os.walk('.'):
        for file in files:
            license = ''
            copyright = []
            target = os.path.join (root, file)
            try:
                with open(target, 'r') as rfd:
                    #read about the first few lines of the file only
                    lines = rfd.readlines(220)
            except UnicodeDecodeError:
                continue
            for line in lines:
                if 'SPDX-License-Identifier' in line:
                    license = line.split (':')[1].strip()
                if 'Copyright (C) ' in line:
                    parts = line.split ('Copyright (C)')[1].strip() #split out the copyright header
                    partition = parts.partition(' ')[2] # remove the year string
                    copyright += ["%s" % partition]
                if license and copyright:
                    break
            if license and copyright:
                license_matches [target] = license
                copyright_matches [target] = copyright
                license = ''

    # group files together by directory
    directory_license = {}
    directory_copyright = {}
    for i in license_matches:
        directory = "%s/" % os.path.dirname (i)
        if directory not in directory_license:
            directory_license [directory] = license_matches [i]
        if directory not in directory_copyright:
            directory_copyright [directory] = copyright_matches [i]
        else:
            directory_copyright [directory] += copyright_matches [i]

    #collapse all directories with common licenses
    for i in directory_copyright:
        for j in directory_copyright:
            if i == j:
                continue
            if j in i:
                print ("%s is in %s" % (j, i))
                if directory_copyright[i] == directory_copyright [j]:
                    directory_copyright[i] = None
                    directory_license[i] = None

    with open(copyright_in, 'r') as rfd:
        lines = rfd.readlines()

    with open(copyright_out, 'w') as wfd:
        for line in lines:
            if line.startswith("%%%DYNAMIC%%%"):
                for i in directory_license:
                    if not directory_license[i]:
                        continue
                    wfd.write("Files: %s*\n" % i)
                    for holder in set(directory_copyright[i]):
                        wfd.write("Copyright: %s\n" % holder)
                    wfd.write("License: %s\n" % directory_license[i])
                    wfd.write("\n")
            else:
                wfd.write(line)

directory = os.path.join (os.getcwd(), 'debian')
update_debian_control(directory)
update_debian_copyright(directory)
