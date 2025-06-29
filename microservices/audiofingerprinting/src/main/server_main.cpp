separator.py
"""
Refactored Audio Separator class with async support for FastAPI
"""

import os
import sys
import subprocess
import logging
import asyncio
from pathlib import Path
from typing import List, Optional, Dict, Callable
import tempfile
import json
import time
import uuid
from concurrent.futures import ThreadPoolExecutor

# Import will be done at runtime to avoid startup failures


class AsyncAudioSeparator:
    """Async version of the Professional Audio Separator"""
    
    def __init__(self, 
                 output_dir: str = None,
                 temp_dir: str = None,
                 output_format: str = "wav",
                 sample_rate: int = 44100,
                 model_file_dir: str = None,
                 log_level: str = "INFO",
                 progress_callback: Optional[Callable] = None):
        """
        Initialize the Async Audio Separator
        
        Args:
            output_dir: Directory to save output files
            temp_dir: Directory for temporary files
            output_format: Output audio format
            sample_rate: Audio sample rate
            model_file_dir: Directory to cache model files
            log_level: Logging level
            progress_callback: Optional callback for progress updates
        """
        # Import dependencies at runtime
        try:
            from audio_separator.separator import Separator
            self.Separator = Separator
        except ImportError:
            raise ImportError("audio-separator library not found! Install with: pip install audio-separator")
        
        try:
            import yt_dlp
            # Test that yt_dlp actually works
            _ = yt_dlp.YoutubeDL({'quiet': True})
            self.yt_dlp = yt_dlp
            if hasattr(self, 'logger'):
                self.logger.info("yt-dlp imported successfully")
        except Exception as e:
            if hasattr(self, 'logger'):
                self.logger.error(f"yt-dlp import/test failed: {e}")
            raise ImportError(f"yt-dlp library not working! Error: {e}")  
        
        self.supported_video_formats = {'.mp4', '.avi', '.mov', '.mkv', '.wmv', '.flv', '.webm', '.m4v'}
        self.supported_audio_formats = {'.wav', '.mp3', '.flac', '.aac', '.ogg', '.m4a', '.wma'}
        
        # Setup directories
        self.output_dir = output_dir or os.path.join(os.getcwd(), "output")
        self.temp_dir = temp_dir or os.path.join(os.getcwd(), "temp")
        
        # Create directories if they don't exist
        os.makedirs(self.output_dir, exist_ok=True)
        os.makedirs(self.temp_dir, exist_ok=True)
        
        # Setup logging
        logging.basicConfig(
            level=getattr(logging, log_level.upper()),
            format='%(asctime)s - %(levelname)s - %(message)s'
        )
        self.logger = logging.getLogger(__name__)
        
        # Progress callback
        self.progress_callback = progress_callback
        
        # Thread pool for CPU-intensive tasks
        self.executor = ThreadPoolExecutor(max_workers=2)
        
        # Initialize separator with configuration
        model_dir = model_file_dir or os.path.join(self.temp_dir, "models")
        os.makedirs(model_dir, exist_ok=True)
        
        self.separator = self.Separator(
            output_dir=self.output_dir,
            output_format=output_format.upper(),
            sample_rate=sample_rate,
            model_file_dir=model_dir,
            log_level=getattr(logging, log_level.upper()),
            output_single_stem="Instrumental"  # Only output instrumental stem
        )
        
        self.current_model = None
    
    def _update_progress(self, message: str, progress: float = None):
        """Update progress via callback if available"""
        if self.progress_callback:
            self.progress_callback(message, progress)
        self.logger.info(message)
    
    def is_url(self, input_string: str) -> bool:
        """Check if input string is a URL"""
        return input_string.startswith(('http://', 'https://', 'www.'))
    
    async def download_audio_ytdlp(self, url: str, task_id: str = None) -> str:
        """
        Download audio from URL using yt-dlp (async)
        
        Args:
            url: URL to download from
            task_id: Optional task ID for file naming
        
        Returns:
            Path to downloaded audio file
        """
        def _download():
            try:
                # Create task-specific temp directory
                temp_subdir = os.path.join(self.temp_dir, task_id or str(uuid.uuid4())[:8])
                os.makedirs(temp_subdir, exist_ok=True)
                
                output_template = os.path.join(temp_subdir, 'audio.%(ext)s')
                
                self._update_progress(f"📥 Downloading audio from: {url}")
                
                # Configure yt-dlp options
                ydl_opts = {
                    'format': 'bestaudio/best',
                    'outtmpl': {
                        'default': output_template,
                    },
                    'postprocessors': [{
                        'key': 'FFmpegExtractAudio',
                        'preferredcodec': 'wav',
                        'preferredquality': '0',  # Best quality
                    }],
                    'quiet': self.logger.level > logging.INFO,
                    'no_warnings': self.logger.level > logging.WARNING,
                    'extract_flat': False,
                    'writethumbnail': False,
                    'writeinfojson': False,
                }
                
                downloaded_file = None
                
                # Download audio
                with self.yt_dlp.YoutubeDL(ydl_opts) as ydl:
                    ydl.download([url])
                    
                    # Find the downloaded file
                    temp_path = Path(temp_subdir)
                    audio_extensions = ['.wav', '.mp3', '.m4a', '.webm', '.ogg', '.flac']
                    
                    for ext in audio_extensions:
                        audio_files = list(temp_path.glob(f"*{ext}"))
                        if audio_files:
                            downloaded_file = str(audio_files[0])
                            break
                    
                    if not downloaded_file:
                        # Fallback: get any file that's not a directory
                        all_files = [f for f in temp_path.iterdir() if f.is_file()]
                        if all_files:
                            downloaded_file = str(all_files[0])
                        else:
                            raise FileNotFoundError("No downloaded audio file found")
                
                if not downloaded_file or not os.path.exists(downloaded_file):
                    raise FileNotFoundError("Downloaded audio file not found")
                
                self._update_progress(f"✅ Audio downloaded: {Path(downloaded_file).name}")
                return downloaded_file
                
            except Exception as e:
                self.logger.error(f"Download failed: {str(e)}")
                raise RuntimeError(f"Failed to download audio from {url}: {e}")
        
        # Run download in thread pool
        return await asyncio.get_event_loop().run_in_executor(self.executor, _download)
    
    async def check_ffmpeg(self) -> bool:
        """Check if FFmpeg is available (async)"""
        def _check():
            try:
                subprocess.run(['ffmpeg', '-version'], 
                             stdout=subprocess.DEVNULL, 
                             stderr=subprocess.DEVNULL, 
                             check=True)
                return True
            except (subprocess.CalledProcessError, FileNotFoundError):
                return False
        
        return await asyncio.get_event_loop().run_in_executor(self.executor, _check)
    
    async def extract_audio_ffmpeg(self, video_path: str, output_path: str = None, **kwargs) -> str:
        """
        Extract audio from video using FFmpeg (async)
        """
        def _extract():
            video_file = Path(video_path)
            if not video_file.exists():
                raise FileNotFoundError(f"Video file not found: {video_path}")
            
            if output_path is None:
                output_path_resolved = video_file.with_suffix('.wav')
            else:
                output_path_resolved = Path(output_path)
            
            # Build FFmpeg command
            sample_rate = kwargs.get('sample_rate', 44100)
            channels = kwargs.get('channels', 2)
            bitrate = kwargs.get('bitrate', None)
            
            cmd = [
                'ffmpeg',
                '-i', str(video_file),
                '-vn',  # No video
                '-acodec', 'pcm_s16le',  # WAV codec
                '-ar', str(sample_rate),
                '-ac', str(channels),
                '-y',  # Overwrite
                str(output_path_resolved)
            ]
            
            if bitrate:
                cmd.insert(-2, '-b:a')
                cmd.insert(-2, bitrate)
            
            try:
                self._update_progress(f"Extracting audio from: {video_file.name}")
                subprocess.run(cmd, check=True, capture_output=True)
                self._update_progress(f"✅ Audio extracted: {output_path_resolved}")
                return str(output_path_resolved)
            except subprocess.CalledProcessError as e:
                raise RuntimeError(f"FFmpeg failed: {e}")
        
        return await asyncio.get_event_loop().run_in_executor(self.executor, _extract)
    
    async def load_model(self, model_name: str = "model_bs_roformer_ep_317_sdr_12.9755.ckpt"):
        """
        Load a separation model (async)
        """
        def _load():
            try:
                self._update_progress(f"🤖 Loading model: {model_name}")
                self.separator.load_model(model_filename=model_name)
                self.current_model = model_name
                self._update_progress(f"✅ Model loaded successfully")
            except Exception as e:
                raise RuntimeError(f"Failed to load model {model_name}: {e}")
        
        await asyncio.get_event_loop().run_in_executor(self.executor, _load)
    
    async def separate_audio_instrumental_only(self, audio_path: str) -> List[str]:
        """
        Separate audio and return only the instrumental track (async)
        """
        def _separate():
            if not self.current_model:
                raise RuntimeError("No model loaded. Call load_model() first.")
            
            try:
                self._update_progress(f"🎵 Extracting instrumental track: {Path(audio_path).name}")
                
                # The separator is already configured to output only instrumental stem
                # Perform separation
                output_files = self.separator.separate(audio_path)
                
                # Since we configured output_single_stem="Instrumental", we should only get instrumental files
                instrumental_files = []
                for file in output_files:
                    file_path = Path(file)
                    instrumental_files.append(file)
                    self._update_progress(f"   ✅ Generated: {file_path.name}")
                
                if not instrumental_files:
                    raise RuntimeError("No instrumental files were generated")
                
                self._update_progress(f"✅ Instrumental extraction complete! Generated {len(instrumental_files)} file(s)")
                
                # Debug: Log the actual file paths
                for file in instrumental_files:
                    self.logger.info(f"Generated file: {file}")
                
                return instrumental_files
                
            except Exception as e:
                raise RuntimeError(f"Audio separation failed: {e}")
        
        return await asyncio.get_event_loop().run_in_executor(self.executor, _separate)
    
    async def process_complete_pipeline(self, 
                                      input_source: str,
                                      task_id: str = None,
                                      keep_intermediate: bool = False,
                                      **extract_kwargs) -> List[str]:
        """
        Complete async pipeline: download/process and separate
        """
        if not task_id:
            task_id = str(uuid.uuid4())[:8]
        
        self._update_progress(f"🎬 Starting processing pipeline for task: {task_id}")
        
        temp_files_to_cleanup = []
        
        try:
            # Step 1: Handle URL vs local file
            if self.is_url(input_source):
                self._update_progress("🌐 Downloading audio from URL", 10)
                audio_file = await self.download_audio_ytdlp(input_source, task_id)
                temp_files_to_cleanup.append(audio_file)
            else:
                input_path = Path(input_source)
                if not input_path.exists():
                    raise FileNotFoundError(f"Input file not found: {input_source}")
                
                file_extension = input_path.suffix.lower()
                
                if file_extension in self.supported_audio_formats:
                    self._update_progress("🎵 Processing audio file", 10)
                    audio_file = str(input_path)
                elif file_extension in self.supported_video_formats:
                    self._update_progress("🎬 Extracting audio from video", 10)
                    temp_audio_path = os.path.join(self.temp_dir, f"{task_id}_audio.wav")
                    audio_file = await self.extract_audio_ffmpeg(input_source, temp_audio_path, **extract_kwargs)
                    if not keep_intermediate:
                        temp_files_to_cleanup.append(audio_file)
                else:
                    raise ValueError(f"Unsupported file format: {file_extension}")
            
            # Step 2: Load model if needed
            self._update_progress("🤖 Loading AI model", 30)
            if self.current_model != "model_bs_roformer_ep_317_sdr_12.9755.ckpt":
                await self.load_model("model_bs_roformer_ep_317_sdr_12.9755.ckpt")
            
            # Step 3: Perform separation
            self._update_progress("🎵 Separating audio (removing vocals)", 50)
            output_files = await self.separate_audio_instrumental_only(audio_file)
            
            # Step 4: Cleanup temporary files
            if not keep_intermediate:
                self._update_progress("🗑️ Cleaning up temporary files", 90)
                for temp_file in temp_files_to_cleanup:
                    if os.path.exists(temp_file):
                        try:
                            os.remove(temp_file)
                            self.logger.debug(f"🗑️ Removed temporary file: {temp_file}")
                        except Exception as e:
                            self.logger.warning(f"⚠️ Failed to remove temporary file {temp_file}: {e}")
            
            self._update_progress("🎉 Processing complete!", 100)
            return output_files
            
        except Exception as e:
            # Cleanup on error
            for temp_file in temp_files_to_cleanup:
                if os.path.exists(temp_file):
                    try:
                        os.remove(temp_file)
                    except:
                        pass
            raise e
    
    def cleanup_task_files(self, task_id: str):
        """Clean up all temporary files for a specific task"""
        try:
            temp_task_dir = os.path.join(self.temp_dir, task_id)
            if os.path.exists(temp_task_dir):
                import shutil
                shutil.rmtree(temp_task_dir)
                if hasattr(self, 'logger'):
                    self.logger.debug(f"Cleaned up task directory: {temp_task_dir}")
        except Exception as e:
            if hasattr(self, 'logger'):
                self.logger.warning(f"Failed to cleanup task {task_id}: {e}")
    
    async def batch_process_urls(self, 
                               urls: List[str],
                               task_id: str = None,
                               keep_intermediate: bool = False,
                               **kwargs) -> List[str]:
        """
        Batch process multiple URLs (async)
        """
        if not task_id:
            task_id = str(uuid.uuid4())[:8]
        
        self._update_progress(f"📁 Starting batch processing: {len(urls)} URLs")
        
        # Load model once for batch processing
        if self.current_model != "model_bs_roformer_ep_317_sdr_12.9755.ckpt":
            await self.load_model("model_bs_roformer_ep_317_sdr_12.9755.ckpt")
        
        all_output_files = []
        successful = 0
        
        for i, url in enumerate(urls, 1):
            try:
                progress = (i / len(urls)) * 100
                self._update_progress(f"Processing URL {i}/{len(urls)}: {url}", progress)
                
                url_task_id = f"{task_id}_{i}"
                output_files = await self.process_complete_pipeline(
                    url,
                    task_id=url_task_id,
                    keep_intermediate=keep_intermediate,
                    **kwargs
                )
                all_output_files.extend(output_files)
                successful += 1
                
            except Exception as e:
                self.logger.error(f"❌ Failed to process {url}: {e}")
        
        self._update_progress(f"✅ Batch processing complete: {successful}/{len(urls)} URLs processed", 100)
        return all_output_files
    
    def get_file_info(self, file_path: str) -> Dict:
        """Get information about an audio file"""
        try:
            file_path_obj = Path(file_path)
            if not file_path_obj.exists():
                self.logger.warning(f"File not found for info: {file_path}")
                return None
            
            file_size = file_path_obj.stat().st_size
            
            # Basic file info
            info = {
                'filename': file_path_obj.name,
                'file_path': str(file_path_obj.absolute()),  # Use absolute path
                'file_size': file_size,
                'format': file_path_obj.suffix.lower().lstrip('.'),
                'sample_rate': 44100,  # Default, could be extracted with librosa if needed
                'duration': None  # Could be extracted with librosa if needed
            }
            
            self.logger.debug(f"File info for {file_path}: {info}")
            return info
            
        except Exception as e:
            self.logger.error(f"Failed to get file info for {file_path}: {e}")
            return None
    
    async def separate_audio_to_memory(self, audio_path: str) -> bytes:
        """
        Separate audio and return instrumental track as WAV bytes in memory
        WITHOUT saving any files to disk (uses temporary directory)
        
        Args:
            audio_path: Path to input audio file
        
        Returns:
            WAV audio data as bytes
        """
        if not self.current_model:
            raise RuntimeError("No model loaded. Call load_model() first.")
        
        def _separate():
            try:
                import io
                import soundfile as sf
                import numpy as np
                import tempfile
                import os
                
                self._update_progress(f"🎵 Extracting instrumental track: {Path(audio_path).name}")
                
                # Create a temporary output directory
                temp_output_dir = tempfile.mkdtemp(prefix="audio_stream_")
                self.logger.debug(f"   📁 Created temporary output directory: {temp_output_dir}")
                
                try:
                    # Store original output directory
                    original_output_dir = self.separator.output_dir
                    self.logger.debug(f"   💾 Original output dir: {original_output_dir}")
                    
                    # Change the separator's output directory
                    self.separator.output_dir = temp_output_dir
                    self.logger.debug(f"   🔄 Changed output dir to: {self.separator.output_dir}")
                    
                    # Also check if separator has other output-related attributes
                    if hasattr(self.separator, 'model_instance') and hasattr(self.separator.model_instance, 'output_dir'):
                        self.logger.debug(f"   🔄 Also changing model_instance.output_dir")
                        original_model_output = self.separator.model_instance.output_dir
                        self.separator.model_instance.output_dir = temp_output_dir
                    
                    # Perform separation
                    self.logger.debug(f"   🤖 Starting separation with temp dir: {temp_output_dir}")
                    output_files = self.separator.separate(audio_path)
                    self.logger.debug(f"   🔍 Separator returned {len(output_files)} files:")
                    
                    for i, file in enumerate(output_files):
                        exists = os.path.exists(file)
                        self.logger.debug(f"     {i+1}. {file} (exists: {exists})")
                        
                        # If file doesn't exist at returned path, check in temp directory
                        if not exists:
                            filename = os.path.basename(file)
                            temp_path = os.path.join(temp_output_dir, filename)
                            temp_exists = os.path.exists(temp_path)
                            self.logger.debug(f"         Checking temp path: {temp_path} (exists: {temp_exists})")
                    
                    # Also check what's actually in the temp directory
                    self.logger.debug(f"   📂 Contents of temp directory {temp_output_dir}:")
                    try:
                        temp_contents = os.listdir(temp_output_dir)
                        for item in temp_contents:
                            item_path = os.path.join(temp_output_dir, item)
                            self.logger.debug(f"     - {item} (size: {os.path.getsize(item_path)} bytes)")
                    except Exception as e:
                        self.logger.debug(f"     Error listing temp directory: {e}")
                    
                    # Find the instrumental file
                    instrumental_file = None
                    
                    # First try the paths returned by separator
                    for file in output_files:
                        if 'instrumental' in Path(file).name.lower():
                            if os.path.exists(file):
                                instrumental_file = file
                                self.logger.debug(f"   ✅ Found instrumental at returned path: {file}")
                                break
                    
                    # If not found, check directly in temp directory
                    if not instrumental_file:
                        try:
                            for item in os.listdir(temp_output_dir):
                                if 'instrumental' in item.lower():
                                    instrumental_file = os.path.join(temp_output_dir, item)
                                    self.logger.debug(f"   ✅ Found instrumental in temp dir: {instrumental_file}")
                                    break
                        except:
                            pass
                    
                    if not instrumental_file:
                        raise RuntimeError(f"No instrumental file found. Output files: {output_files}, Temp dir contents: {os.listdir(temp_output_dir) if os.path.exists(temp_output_dir) else 'N/A'}")
                    
                    if not os.path.exists(instrumental_file):
                        raise RuntimeError(f"Instrumental file not found: {instrumental_file}")
                    
                    self.logger.debug(f"   📖 Reading instrumental file: {instrumental_file}")
                    
                    # Read the audio file into memory
                    audio_data, sample_rate = sf.read(instrumental_file)
                    self.logger.debug(f"   📊 Loaded audio: {len(audio_data)} samples at {sample_rate}Hz")
                    
                    # Convert to WAV bytes in memory
                    buffer = io.BytesIO()
                    sf.write(buffer, audio_data, sample_rate, format='WAV')
                    wav_bytes = buffer.getvalue()
                    buffer.close()
                    
                    self.logger.debug(f"   💾 Generated WAV: {len(wav_bytes)} bytes")
                    
                    return wav_bytes
                    
                finally:
                    # Restore original output directory
                    self.separator.output_dir = original_output_dir
                    if hasattr(self.separator, 'model_instance') and hasattr(self.separator.model_instance, 'output_dir'):
                        self.separator.model_instance.output_dir = original_model_output
                    self.logger.debug(f"   🔄 Restored output directory")
                    
                    # Clean up temp directory
                    try:
                        import shutil
                        shutil.rmtree(temp_output_dir)
                        self.logger.debug(f"   🗑️ Cleaned up temp directory: {temp_output_dir}")
                    except Exception as e:
                        self.logger.debug(f"   ⚠️ Failed to clean up temp directory: {e}")
                
            except Exception as e:
                self.logger.error(f"   ❌ Error in _separate: {e}")
                raise RuntimeError(f"Audio separation failed: {e}")
        
        return await asyncio.get_event_loop().run_in_executor(self.executor, _separate)
    
    async def process_url_to_memory(self, 
                                  input_source: str,
                                  task_id: str = None,
                                  keep_intermediate: bool = False,
                                  **extract_kwargs) -> bytes:
        """
        Complete pipeline returning WAV bytes in memory instead of files
        
        Args:
            input_source: URL or path to input audio/video file
            task_id: Optional task ID for temporary files
            keep_intermediate: Keep intermediate downloaded/extracted audio files
            **extract_kwargs: Arguments for audio extraction
        
        Returns:
            WAV audio data as bytes
        """
        if not task_id:
            task_id = str(uuid.uuid4())[:8]
        
        self._update_progress(f"🎬 Starting processing pipeline for task: {task_id}")
        
        temp_files_to_cleanup = []
        
        try:
            # Step 1: Handle URL vs local file
            if self.is_url(input_source):
                self._update_progress("🌐 Downloading audio from URL", 10)
                audio_file = await self.download_audio_ytdlp(input_source, task_id)
                temp_files_to_cleanup.append(audio_file)
            else:
                input_path = Path(input_source)
                if not input_path.exists():
                    raise FileNotFoundError(f"Input file not found: {input_source}")
                
                file_extension = input_path.suffix.lower()
                
                if file_extension in self.supported_audio_formats:
                    self._update_progress("🎵 Processing audio file", 10)
                    audio_file = str(input_path)
                elif file_extension in self.supported_video_formats:
                    self._update_progress("🎬 Extracting audio from video", 10)
                    temp_audio_path = os.path.join(self.temp_dir, f"{task_id}_audio.wav")
                    audio_file = await self.extract_audio_ffmpeg(input_source, temp_audio_path, **extract_kwargs)
                    if not keep_intermediate:
                        temp_files_to_cleanup.append(audio_file)
                else:
                    raise ValueError(f"Unsupported file format: {file_extension}")
            
            # Step 2: Load model if needed
            self._update_progress("🤖 Loading AI model", 30)
            if self.current_model != "model_bs_roformer_ep_317_sdr_12.9755.ckpt":
                await self.load_model("model_bs_roformer_ep_317_sdr_12.9755.ckpt")
            
            # Step 3: Perform separation and return WAV bytes
            self._update_progress("🎵 Separating audio (removing vocals)", 50)
            wav_bytes = await self.separate_audio_to_memory(audio_file)
            
            # Step 4: Cleanup temporary files
            if not keep_intermediate:
                self._update_progress("🗑️ Cleaning up temporary files", 90)
                for temp_file in temp_files_to_cleanup:
                    if os.path.exists(temp_file):
                        try:
                            os.remove(temp_file)
                            self.logger.debug(f"Removed temporary file: {temp_file}")
                        except Exception as e:
                            self.logger.warning(f"Failed to remove temporary file {temp_file}: {e}")
            
            self._update_progress("🎉 Processing complete!", 100)
            return wav_bytes
            
        except Exception as e:
            # Cleanup on error
            for temp_file in temp_files_to_cleanup:
                if os.path.exists(temp_file):
                    try:
                        os.remove(temp_file)
                    except:
                        pass
            raise e
    
    def __del__(self):
        """Cleanup executor on deletion"""
        if hasattr(self, 'executor'):
            self.executor.shutdown(wait=False)