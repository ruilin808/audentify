#!/usr/bin/env python3
"""
CLI interface for High-Quality Audio Extraction
Optimized for fingerprinting and audio recognition workflows
"""

import os
import sys
import argparse
import asyncio
import logging
from pathlib import Path
from typing import List
import tempfile
import shutil

# Import the service directly
from app.main import AudioExtractionService


def check_dependencies():
    """Check if required dependencies are available"""
    missing = []
    
    try:
        import yt_dlp
    except ImportError:
        missing.append("yt-dlp (pip install yt-dlp)")
    
    try:
        import soundfile
    except ImportError:
        missing.append("soundfile (pip install soundfile)")
    
    # Check optional dependencies
    optional_missing = []
    try:
        import librosa
    except ImportError:
        optional_missing.append("librosa (pip install librosa) - recommended for best quality")
    
    try:
        from pydub import AudioSegment
    except ImportError:
        optional_missing.append("pydub (pip install pydub) - fallback audio processing")
    
    if missing:
        print("❌ Missing required dependencies:")
        for dep in missing:
            print(f"   - {dep}")
        return False
    
    if optional_missing:
        print("⚠️ Optional dependencies missing (will use fallbacks):")
        for dep in optional_missing:
            print(f"   - {dep}")
    
    return True


class CLIProgressHandler:
    """Progress handler for CLI output"""
    
    def __init__(self, verbose: bool = True):
        self.verbose = verbose
    
    def __call__(self, message: str, progress: float = None):
        if self.verbose:
            if progress is not None:
                print(f"[{progress:5.1f}%] {message}")
            else:
                print(f"         {message}")


async def extract_single_audio(service: AudioExtractionService, url: str, args) -> str:
    """Extract audio from a single URL"""
    try:
        if args.verbose:
            print(f"🎵 Processing: {url}")
        
        audio_bytes, filename = await service.extract_high_quality_audio(url)
        
        # Save to output directory
        output_path = os.path.join(args.output_dir, filename)
        with open(output_path, 'wb') as f:
            f.write(audio_bytes)
        
        if args.verbose:
            file_size_mb = len(audio_bytes) / (1024 * 1024)
            print(f"✅ Extracted: {filename} ({file_size_mb:.1f} MB)")
        
        return output_path
        
    except Exception as e:
        print(f"❌ Failed to process {url}: {e}")
        return None


async def main():
    parser = argparse.ArgumentParser(
        description='High-Quality Audio Extraction CLI - Optimized for Fingerprinting',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Extract audio from YouTube URL
  python cli.py "https://www.youtube.com/watch?v=dQw4w9WgXcQ"

  # Process local audio file (copy with minimal processing)
  python cli.py song.mp3

  # Batch process multiple URLs
  python cli.py urls.txt --batch

  # Custom output directory and quality settings
  python cli.py "https://youtu.be/..." -o ./output --quality enhanced

  # Optimize for fingerprinting
  python cli.py "https://youtu.be/..." --fingerprint-optimized

Note: This tool extracts high-quality audio with minimal processing
to preserve original characteristics for optimal fingerprinting accuracy.
        """
    )
    
    # Input/Output
    parser.add_argument('input', 
                       help='URL to download, audio file, or text file with URLs (for batch)')
    parser.add_argument('-o', '--output-dir', 
                       default='./output',
                       help='Output directory (default: ./output)')
    parser.add_argument('-f', '--format', default='wav', 
                       choices=['wav', 'mp3', 'flac', 'm4a', 'aac'],
                       help='Output format (default: wav)')
    
    # Quality Settings
    parser.add_argument('--quality', default='minimal',
                       choices=['minimal', 'standard', 'enhanced'],
                       help='Processing quality level (default: minimal)')
    parser.add_argument('--sample-rate', type=int,
                       help='Target sample rate (default: preserve original)')
    parser.add_argument('--channels', type=int,
                       help='Target channels (default: preserve original)')
    parser.add_argument('--bit-depth', type=int, default=16,
                       choices=[16, 24, 32],
                       help='Audio bit depth (default: 16)')
    
    # Processing Options
    parser.add_argument('--fingerprint-optimized', action='store_true',
                       help='Use settings optimized for fingerprinting')
    parser.add_argument('--batch', action='store_true',
                       help='Process input as text file containing URLs (one per line)')
    parser.add_argument('--no-normalize', action='store_true',
                       help='Skip volume normalization')
    parser.add_argument('--no-filter', action='store_true',
                       help='Skip high-pass filtering')
    
    # System
    parser.add_argument('--temp-dir',
                       help='Directory for temporary files (default: system temp)')
    parser.add_argument('--log-level', default='INFO',
                       choices=['DEBUG', 'INFO', 'WARNING', 'ERROR'],
                       help='Logging level (default: INFO)')
    parser.add_argument('-v', '--verbose', action='store_true',
                       help='Show detailed progress information')
    parser.add_argument('-q', '--quiet', action='store_true',
                       help='Suppress progress output')
    
    args = parser.parse_args()
    
    # Handle quiet/verbose flags
    if args.quiet:
        log_level = 'ERROR'
        verbose = False
    else:
        log_level = args.log_level
        verbose = args.verbose or (args.log_level == 'DEBUG')
    
    # Apply fingerprint optimizations
    if args.fingerprint_optimized:
        args.format = 'wav'
        args.sample_rate = args.sample_rate or 44100
        args.channels = args.channels or 2
        args.bit_depth = 16
        args.quality = 'minimal'
        if verbose:
            print("🎯 Applied fingerprint-optimized settings:")
            print(f"   Format: {args.format}, SR: {args.sample_rate}Hz, Channels: {args.channels}, Depth: {args.bit_depth}bit")
    
    # Set up directories
    os.makedirs(args.output_dir, exist_ok=True)
    temp_dir = args.temp_dir or tempfile.gettempdir()
    
    # Check if input exists (for local files)
    if not args.input.startswith(('http://', 'https://', 'www.')) and not Path(args.input).exists():
        print(f"❌ Input not found: {args.input}")
        sys.exit(1)
    
    # Check dependencies
    if not check_dependencies():
        sys.exit(1)
    
    try:
        # Initialize service
        service = AudioExtractionService(log_level=log_level)
        
        # Collect URLs/files to process
        inputs = []
        if args.batch:
            # Batch processing from file
            if not Path(args.input).exists():
                print(f"❌ Batch input file not found: {args.input}")
                sys.exit(1)
            
            with open(args.input, 'r') as f:
                inputs = [line.strip() for line in f if line.strip() and not line.startswith('#')]
            
            if not inputs:
                print("❌ No URLs found in batch file")
                sys.exit(1)
            
            print(f"📁 Found {len(inputs)} items to process")
        else:
            inputs = [args.input]
        
        # Process all inputs
        start_time = asyncio.get_event_loop().time()
        output_files = []
        failed_count = 0
        
        for i, input_item in enumerate(inputs, 1):
            if len(inputs) > 1:
                print(f"\n[{i}/{len(inputs)}] Processing: {input_item}")
            
            # Handle local files
            if not input_item.startswith(('http://', 'https://', 'www.')):
                if Path(input_item).exists():
                    # For local files, just copy with minimal processing
                    output_name = f"extracted_{Path(input_item).stem}_{i:03d}.{args.format}"
                    output_path = os.path.join(args.output_dir, output_name)
                    
                    # Simple copy for now - could add local file processing later
                    shutil.copy2(input_item, output_path)
                    output_files.append(output_path)
                    
                    if verbose:
                        file_size = os.path.getsize(output_path) / (1024 * 1024)
                        print(f"✅ Copied: {output_name} ({file_size:.1f} MB)")
                else:
                    print(f"❌ Local file not found: {input_item}")
                    failed_count += 1
            else:
                # Process URL
                result = await extract_single_audio(service, input_item, args)
                if result:
                    output_files.append(result)
                else:
                    failed_count += 1
        
        end_time = asyncio.get_event_loop().time()
        processing_time = end_time - start_time
        
        # Display results
        print(f"\n🎉 Processing complete!")
        print(f"   ✅ Successful: {len(output_files)} files")
        print(f"   ❌ Failed: {failed_count} files")
        print(f"   ⏱️ Total time: {processing_time:.1f} seconds")
        
        if output_files:
            total_size = sum(os.path.getsize(f) for f in output_files) / (1024 * 1024)
            print(f"   📁 Output directory: {args.output_dir}")
            print(f"   💾 Total size: {total_size:.1f} MB")
            print(f"   🎼 Format: {args.format.upper()}")
            
            if args.fingerprint_optimized:
                print(f"   🎯 Optimized for fingerprinting")
        
        if verbose and output_files:
            print(f"\nGenerated files:")
            for file_path in output_files:
                print(f"   📄 {Path(file_path).name}")
        
    except KeyboardInterrupt:
        print(f"\n⏹️ Process interrupted by user")
        sys.exit(1)
    except Exception as e:
        print(f"❌ Error: {e}")
        if log_level == 'DEBUG':
            import traceback
            traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    # Run the async main function
    if sys.platform == 'win32':
        # Windows-specific event loop policy
        asyncio.set_event_loop_policy(asyncio.WindowsProactorEventLoopPolicy())
    
    asyncio.run(main())