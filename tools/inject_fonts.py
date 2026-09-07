#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Inject M1's CJK bitmap fonts (FONT section) into HD textdata.cam.

The HD version's textdata.cam only has SMNU + STRT sections.
The M1 version's textdata.cam has SMNU + STRT + FONT sections.
The FONT section contains 5 bitmap font files with CJK glyphs.

This script:
1. Reads M1 textdata.cam, extracts FONT section files
2. Reads HD textdata.cam (from backup_original)
3. Adds FONT section to HD textdata.cam
4. Writes the modified CAM to the output directory
"""
import struct
import os
import sys

# Add tools directory to path
sys.path.insert(0, r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\tools')
from cam_packer import read_cam, pack_cam


def extract_font_section_from_m1(m1_cam_path):
    """Extract FONT section files from M1 textdata.cam."""
    print(f"Reading M1 CAM: {m1_cam_path}")
    cam = read_cam(m1_cam_path)
    
    for sec in cam['sections']:
        if sec['ext'] == 'FONT':
            print(f"  Found FONT section: {len(sec['files'])} files")
            fonts = []
            for f in sec['files']:
                print(f"    {f['name']}: {f['size']:,} bytes")
                fonts.append({
                    'name': f['name'],
                    'data': f['raw'],
                })
            return fonts
    
    print("  FONT section not found!")
    return []


def inject_font_section(hd_cam_path, m1_fonts, output_path):
    """Inject FONT section into HD textdata.cam."""
    print(f"\nReading HD CAM: {hd_cam_path}")
    cam = read_cam(hd_cam_path)
    
    print(f"  Original sections: {len(cam['sections'])}")
    for sec in cam['sections']:
        print(f"    {sec['ext']}: {len(sec['files'])} files")
    
    # Check if FONT section already exists
    has_font = any(sec['ext'] == 'FONT' for sec in cam['sections'])
    if has_font:
        print("  FONT section already exists, replacing...")
        # Remove existing FONT section
        cam['sections'] = [sec for sec in cam['sections'] if sec['ext'] != 'FONT']
    
    # Add FONT section
    font_section = {
        'ext': 'FONT',
        'ext_raw': b'FONT',
        'offset': 0,  # Will be recalculated by pack_cam
        'files': [],
    }
    
    for font in m1_fonts:
        font_section['files'].append({
            'name': font['name'],
            'offset': 0,  # Will be recalculated
            'size': len(font['data']),
            'raw': font['data'],
        })
    
    cam['sections'].append(font_section)
    
    print(f"\n  Modified sections: {len(cam['sections'])}")
    for sec in cam['sections']:
        print(f"    {sec['ext']}: {len(sec['files'])} files")
    
    # We don't need to replace any existing files, just add the FONT section
    # pack_cam will handle the layout
    
    # Build replacements dict (empty - we're not replacing existing files)
    replacements = {}
    
    # Pack the modified CAM
    print(f"\nPacking modified CAM...")
    new_data = pack_cam(cam, replacements)
    
    # Write output
    print(f"Writing output: {output_path}")
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'wb') as f:
        f.write(new_data)
    
    print(f"  Written: {len(new_data):,} bytes")
    
    # Verify: read back and check
    print(f"\nVerifying...")
    verify_cam = read_cam(output_path)
    print(f"  Sections: {len(verify_cam['sections'])}")
    for sec in verify_cam['sections']:
        print(f"    {sec['ext']}: {len(sec['files'])} files")
        if sec['ext'] == 'FONT':
            for f in sec['files']:
                print(f"      {f['name']}: {f['size']:,} bytes")
    
    # Verify STRT data is intact
    for sec in verify_cam['sections']:
        if sec['ext'] == 'STRT':
            from cam_packer import parse_strt
            for f in sec['files']:
                strings = parse_strt(f['raw'])
                if strings:
                    print(f"  STRT file {f['name']}: {len(strings)} strings, first='{strings[0]['text'][:30]}'")
                    break
            break
    
    return True


def main():
    # Paths
    m1_cam_path = r'G:\Projects\Majesty1\Data\textdata.cam'
    
    # Use backup_original as the base (not the currently deployed translated version)
    hd_cam_path = r'I:\SteamLibrary\steamapps\common\Majesty HD\backup_original\Data\textdata.cam'
    
    # But we need the TRANSLATED version, not the original English version!
    # The translated textdata.cam is in the workspace output directory
    hd_translated_path = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\output\Data\textdata.cam'
    
    # Output path
    output_path = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\output\Data\textdata.cam'
    
    # Step 1: Extract M1 fonts
    m1_fonts = extract_font_section_from_m1(m1_cam_path)
    if not m1_fonts:
        print("ERROR: No fonts found in M1!")
        return
    
    # Step 2: Check which HD CAM to use as base
    # We should use the translated version (which has UTF-16LE Chinese text)
    if os.path.exists(hd_translated_path):
        print(f"\nUsing translated HD CAM as base: {hd_translated_path}")
        base_cam_path = hd_translated_path
    else:
        print(f"\nTranslated CAM not found, using backup original: {hd_cam_path}")
        base_cam_path = hd_cam_path
    
    # Step 3: Inject FONT section
    success = inject_font_section(base_cam_path, m1_fonts, output_path)
    
    if success:
        print("\n✓ Font injection complete!")
        print(f"  Output: {output_path}")
        print(f"  Size: {os.path.getsize(output_path):,} bytes")
        
        # Also inject into mx_textdata.cam
        mx_translated = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\output\DataMX\mx_textdata.cam'
        if os.path.exists(mx_translated):
            print(f"\n--- Injecting into mx_textdata.cam ---")
            # M1 doesn't have a separate mx_textdata.cam, but the same fonts should work
            inject_font_section(mx_translated, m1_fonts, mx_translated)
        
        # Also inject into gpltext.cam
        gpl_translated = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\output\Data\gpltext.cam'
        if os.path.exists(gpl_translated):
            print(f"\n--- Injecting into gpltext.cam ---")
            inject_font_section(gpl_translated, m1_fonts, gpl_translated)
        
        # mx_gpltext.cam
        mx_gpl = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\output\DataMX\mx_gpltext.cam'
        if os.path.exists(mx_gpl):
            print(f"\n--- Injecting into mx_gpltext.cam ---")
            inject_font_section(mx_gpl, m1_fonts, mx_gpl)
        
        # mx_rgstext.cam
        mx_rgst = r'I:\SteamLibrary\steamapps\common\Majesty HD\workspace\output\DataMX\mx_rgstext.cam'
        if os.path.exists(mx_rgst):
            print(f"\n--- Injecting into mx_rgstext.cam ---")
            inject_font_section(mx_rgst, m1_fonts, mx_rgst)
    else:
        print("\n✗ Font injection failed!")


if __name__ == '__main__':
    main()
