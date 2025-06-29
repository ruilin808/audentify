"""
Updated separator.py using shared download configuration
Only showing the parts that need to be changed
"""

# Add this import at the top of separator.py
from .download_config import DownloadConfig
import logging

# Replace the download_audio_ytdlp method in the AsyncAudioSeparator class:
async def download_audio_ytdlp(self, url: str, task_id: str = None) -> str:
    """
    Download audio from URL using yt-dlp with shared TikTok-compatible settings (async)
    """
    def _download():
        try:
            # Create task-specific temp directory
            temp_subdir = os.path.join(self.temp_dir, task_id or str(uuid.uuid4())[:8])
            os.makedirs(temp_subdir, exist_ok=True)
            
            self._update_progress(f"📥 Downloading audio from: {url}")
            
            # Use shared configuration
            ydl_opts = DownloadConfig.get_separator_ydl_opts(
                temp_subdir=temp_subdir,
                logger_level=self.logger.level
            )
            
            downloaded_file = None
            
            # Download audio with error handling
            try:
                with self.yt_dlp.YoutubeDL(ydl_opts) as ydl:
                    # Try to get info first
                    info = ydl.extract_info(url, download=False)
                    self.logger.info(f"Video info extracted: {info.get('title', 'Unknown')}")
                    
                    # Download
                    ydl.download([url])
                    
                    # Use shared file finding logic
                    downloaded_file = DownloadConfig.find_downloaded_file(temp_subdir)
                    
            except Exception as e:
                self.logger.warning(f"Primary download failed: {e}, trying fallback...")
                
                # Use shared fallback configuration
                fallback_opts = DownloadConfig.get_fallback_ydl_opts(temp_subdir)
                # Adjust for separator's outtmpl format
                fallback_opts['outtmpl'] = {'default': fallback_opts['outtmpl']}
                
                with self.yt_dlp.YoutubeDL(fallback_opts) as ydl_fallback:
                    ydl_fallback.download([url])
                    downloaded_file = DownloadConfig.find_downloaded_file(temp_subdir)
            
            if not downloaded_file or not os.path.exists(downloaded_file):
                raise FileNotFoundError("Downloaded audio file not found")
            
            self._update_progress(f"✅ Audio downloaded: {Path(downloaded_file).name}")
            return downloaded_file
            
        except Exception as e:
            self.logger.error(f"Download failed: {str(e)}")
            raise RuntimeError(f"Failed to download audio from {url}: {e}")
    
    # Run download in thread pool
    return await asyncio.get_event_loop().run_in_executor(self.executor, _download)