"""
Shared download configuration for yt-dlp
This module provides consistent download settings across all components
"""

import os
from pathlib import Path
from typing import Dict, Any


class DownloadConfig:
    """Centralized configuration for yt-dlp downloads"""
    
    @staticmethod
    def get_base_ydl_opts(temp_subdir: str, 
                         is_older_cpu: bool = False,
                         quiet: bool = True,
                         log_level_above_info: bool = True) -> Dict[str, Any]:
        """
        Get base yt-dlp options that work well with TikTok and other platforms
        
        Args:
            temp_subdir: Directory to save downloaded files
            is_older_cpu: Whether to use CPU-optimized settings
            quiet: Whether to suppress output
            log_level_above_info: Whether logging level is above INFO
        
        Returns:
            Dictionary of yt-dlp options
        """
        return {
            # TikTok-compatible format selection
            'format': 'best[height<=720]/best[ext=mp4]/best[ext=webm]/best/worst',
            'outtmpl': os.path.join(temp_subdir, 'audio.%(ext)s'),
            'postprocessors': [{
                'key': 'FFmpegExtractAudio',
                'preferredcodec': 'wav',
                'preferredquality': '0' if not is_older_cpu else '192',
            }],
            'quiet': quiet,
            'no_warnings': log_level_above_info,
            'extract_flat': False,
            'writethumbnail': False,
            'writeinfojson': False,
            'concurrent_fragment_downloads': 2 if not is_older_cpu else 1,
            'retries': 3,
            'socket_timeout': 30,
            # TikTok and social media compatibility
            'user_agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36',
            'no_check_certificate': True,
            'cookiefile': None,
            'ignoreerrors': False,
            # Additional headers for better compatibility
            'http_headers': {
                'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36',
                'Accept': 'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
                'Accept-Language': 'en-us,en;q=0.5',
                'Accept-Encoding': 'gzip,deflate',
                'Accept-Charset': 'ISO-8859-1,utf-8;q=0.7,*;q=0.7',
                'Connection': 'keep-alive',
            }
        }
    
    @staticmethod
    def get_fastapi_ydl_opts(temp_subdir: str, is_older_cpu: bool = False) -> Dict[str, Any]:
        """Get yt-dlp options optimized for FastAPI usage"""
        return DownloadConfig.get_base_ydl_opts(
            temp_subdir=temp_subdir,
            is_older_cpu=is_older_cpu,
            quiet=True,
            log_level_above_info=True
        )
    
    @staticmethod
    def get_separator_ydl_opts(temp_subdir: str, logger_level: int) -> Dict[str, Any]:
        """Get yt-dlp options for the separator class"""
        import logging
        
        base_opts = DownloadConfig.get_base_ydl_opts(
            temp_subdir=temp_subdir,
            is_older_cpu=False,
            quiet=logger_level > logging.INFO,
            log_level_above_info=logger_level > logging.WARNING
        )
        
        # Separator uses different outtmpl format
        base_opts['outtmpl'] = {'default': os.path.join(temp_subdir, 'audio.%(ext)s')}
        
        return base_opts
    
    @staticmethod
    def get_fallback_ydl_opts(temp_subdir: str) -> Dict[str, Any]:
        """Get minimal fallback options for when primary download fails"""
        return {
            'format': 'worst/best',  # Accept any available format
            'outtmpl': os.path.join(temp_subdir, 'audio.%(ext)s'),
            'quiet': True,
            'no_warnings': True,
            'extract_flat': False,
            'retries': 1,
            'user_agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36',
        }
    
    @staticmethod
    def find_downloaded_file(temp_subdir: str) -> str:
        """
        Find the downloaded audio/video file in the temp directory
        
        Args:
            temp_subdir: Directory to search in
            
        Returns:
            Path to the downloaded file
            
        Raises:
            FileNotFoundError: If no file is found
        """
        temp_path = Path(temp_subdir)
        
        # Priority order for file extensions
        priority_extensions = ['.wav', '.mp4', '.webm', '.m4a', '.mp3', '.ogg', '.flac']
        
        for ext in priority_extensions:
            files = list(temp_path.glob(f"*{ext}"))
            if files:
                return str(files[0])
        
        # Fallback: get any file that's not a directory
        all_files = [f for f in temp_path.iterdir() if f.is_file()]
        if all_files:
            return str(all_files[0])
        else:
            raise FileNotFoundError("No downloaded file found")


# Utility function for testing what formats are available
def debug_available_formats(url: str) -> None:
    """
    Debug function to see what formats are available for a URL
    Useful for troubleshooting download issues
    """
    try:
        import yt_dlp
        
        debug_opts = {
            'quiet': False,
            'no_warnings': False,
            'listformats': True,
        }
        
        with yt_dlp.YoutubeDL(debug_opts) as ydl:
            info = ydl.extract_info(url, download=False)
            print(f"\nAvailable formats for: {url}")
            print(f"Title: {info.get('title', 'Unknown')}")
            print("Formats:")
            for fmt in info.get('formats', []):
                print(f"  {fmt.get('format_id', 'N/A'):>10} | "
                      f"{fmt.get('ext', 'N/A'):>5} | "
                      f"{fmt.get('resolution', 'audio'):>10} | "
                      f"{fmt.get('filesize_approx', 'unknown'):>10}")
    except Exception as e:
        print(f"Error debugging formats: {e}")


# Example usage:
# debug_available_formats("https://www.tiktok.com/@ruby_xfx/video/7260367078463474945")