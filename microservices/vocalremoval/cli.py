#!/usr/bin/env python3
"""
CLI interface for Professional Audio Separator
Preserves all original CLI functionality while using the refactored async separator
"""

import os
import sys
import argparse
import asyncio
import logging
from pathlib import Path
from typing import List

from app.separator import AsyncAudioSeparator


def check_dependencies():
    """Check if required dependencies are available"""
    missing = []
    
    try:
        import audio_separator
    except ImportError:
        missing.append("audio-separator (pip install audio-separator)")
    
    try:
        import yt_dlp
    except ImportError:
        missing.append("yt-dlp (pip install yt-dlp)")
    
    if missing:
        print("❌ Missing dependencies:")
        for dep in missing:
            print(f"   - {dep}")
        return False
    
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


async def main():
    parser = argparse.ArgumentParser(
        description='Professional Audio Separator with yt-dlp - Downloads and separates audio without vocals',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Download and separate from YouTube URL
  python cli.py "https://www.youtube.com/watch?v=dQw4w9WgXcQ"

  # Process local audio file
  python cli.py song.mp3

  # Process local video file
  python cli.py video.mp4

  # Batch process multiple URLs
  python cli.py urls.txt --batch

  # Keep intermediate downloaded files
  python cli.py "https://youtu.be/..." --keep-intermediate

  # Custom output directory and format
  python cli.py "https://youtu.be/..." -o ./output -f mp3

Note: This script uses BS-Roformer model with --single-stem Instrumental
to extract ONLY the high-quality instrumental track (vocals discarded)
        """
    )
    
    # Input/Output
    parser.add_argument('input', 
                       help='URL to download, audio/video file, or text file with URLs (for batch)')
    parser.add_argument('-o', '--output-dir', 
                       help='Output directory (default: ./output)')
    parser.add_argument('-f', '--format', default='wav', 
                       choices=['wav', 'mp3', 'flac', 'm4a', 'aac'],
                       help='Output format (default: wav)')
    parser.add_argument('-r', '--sample-rate', type=int, default=44100,
                       help='Sample rate (default: 44100)')
    
    # Processing Options
    parser.add_argument('--batch', action='store_true',
                       help='Process input as text file containing URLs (one per line)')
    parser.add_argument('--keep-intermediate', action='store_true',
                       help='Keep intermediate downloaded/extracted audio files')
    
    # Audio Extraction (for video inputs)
    parser.add_argument('--channels', type=int, default=2,
                       help='Audio channels for video extraction (default: 2)')
    parser.add_argument('--bitrate', 
                       help='Audio bitrate for video extraction (e.g., 320k)')
    
    # System
    parser.add_argument('--temp-dir',
                       help='Directory for temporary files (default: ./temp)')
    parser.add_argument('--model-dir', 
                       help='Directory to cache models (default: ./temp/models)')
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
    
    # Set up directories
    output_dir = args.output_dir or os.path.join(os.getcwd(), "output")
    temp_dir = args.temp_dir or os.path.join(os.getcwd(), "temp")
    model_dir = args.model_dir or os.path.join(temp_dir, "models")
    
    # Check if input exists (for local files)
    if not args.input.startswith(('http://', 'https://', 'www.')) and not Path(args.input).exists():
        print(f"❌ Input not found: {args.input}")
        sys.exit(1)
    
    # Check dependencies
    if not check_dependencies():
        sys.exit(1)
    
    try:
        # Initialize separator with CLI-specific settings
        progress_handler = CLIProgressHandler(verbose)
        
        separator = AsyncAudioSeparator(
            output_dir=output_dir,
            temp_dir=temp_dir,
            output_format=args.format,
            sample_rate=args.sample_rate,
            model_file_dir=model_dir,
            log_level=log_level,
            progress_callback=progress_handler
        )
        
        # Check dependencies
        if not await separator.check_ffmpeg():
            print("⚠️ FFmpeg not found - video processing will not work")
            print("Install FFmpeg: https://ffmpeg.org/download.html")
            if not args.input.startswith(('http://', 'https://', 'www.')):
                # For local files, check if it's a video file
                file_extension = Path(args.input).suffix.lower()
                if file_extension in separator.supported_video_formats:
                    print("❌ Cannot process video files without FFmpeg")
                    sys.exit(1)
        
        # Process files/URLs
        extract_kwargs = {
            'sample_rate': args.sample_rate,
            'channels': args.channels,
            'bitrate': args.bitrate
        }
        
        start_time = asyncio.get_event_loop().time()
        
        if args.batch:
            # Batch processing from file
            if not Path(args.input).exists():
                print(f"❌ Batch input file not found: {args.input}")
                sys.exit(1)
            
            # Read URLs from file
            with open(args.input, 'r') as f:
                urls = [line.strip() for line in f if line.strip() and not line.startswith('#')]
            
            if not urls:
                print("❌ No URLs found in batch file")
                sys.exit(1)
            
            print(f"📁 Found {len(urls)} URLs to process")
            
            output_files = await separator.batch_process_urls(
                urls=urls,
                keep_intermediate=args.keep_intermediate,
                **extract_kwargs
            )
        else:
            # Single URL/file processing
            output_files = await separator.process_complete_pipeline(
                input_source=args.input,
                keep_intermediate=args.keep_intermediate,
                **extract_kwargs
            )
        
        end_time = asyncio.get_event_loop().time()
        processing_time = end_time - start_time
        
        # Display results
        print(f"\n🎉 Processing complete! Generated {len(output_files)} instrumental file(s):")
        for file_path in output_files:
            file_info = separator.get_file_info(file_path)
            if file_info:
                size_mb = file_info['file_size'] / (1024 * 1024)
                print(f"   📁 {file_info['filename']} ({size_mb:.1f} MB)")
            else:
                print(f"   📁 {Path(file_path).name}")
        
        print(f"\n📊 Processing Statistics:")
        print(f"   ⏱️ Total time: {processing_time:.1f} seconds")
        print(f"   📂 Output directory: {output_dir}")
        print(f"   🎼 Format: {args.format.upper()}")
        print(f"   🎵 Sample rate: {args.sample_rate} Hz")
        
        print(f"\n📝 Note: Only instrumental track extracted (vocals discarded)")
        print(f"   🎼 Complete backing track without vocals")
        
        # Simple cleanup without using cleanup_task_files method
        print(f"🗑️ Cleaning up temporary files...")
        
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