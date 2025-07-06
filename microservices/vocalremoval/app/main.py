#!/usr/bin/env python3
"""
High-Quality Audio Extraction API - Optimized for Fingerprinting
Minimal processing approach for preserving original audio characteristics
"""

import os
import uuid
import asyncio
import logging
import io
import tempfile
import shutil
import gc
from pathlib import Path
from typing import Optional, Tuple
from contextlib import asynccontextmanager
from concurrent.futures import ThreadPoolExecutor

import requests
from fastapi import FastAPI, HTTPException
from fastapi.responses import StreamingResponse
from fastapi.middleware.cors import CORSMiddleware

# Import your updated models
from app.models import ExtractAudioRequest, HealthResponse


class AudioExtractionService:
    """High-quality audio extraction service optimized for fingerprinting"""
    
    def __init__(self, log_level: str = "INFO"):
        try:
            import yt_dlp
            self.yt_dlp = yt_dlp
            import soundfile as sf
            self.sf = sf
            # Try to import audio processing library
            try:
                import librosa
                self.librosa = librosa
                self.has_librosa = True
            except ImportError:
                try:
                    from pydub import AudioSegment
                    self.AudioSegment = AudioSegment
                    self.has_pydub = True
                    self.has_librosa = False
                except ImportError:
                    self.has_librosa = False
                    self.has_pydub = False
                    
        except ImportError as e:
            raise ImportError(f"Required library not found! Error: {e}")
        
        self.temp_dir_base = os.path.join(os.getcwd(), "temp")
        self.downloads_dir = os.path.join(os.getcwd(), "downloads")
        os.makedirs(self.temp_dir_base, exist_ok=True)
        os.makedirs(self.downloads_dir, exist_ok=True)
        
        self.logger = logging.getLogger(__name__)
        self.log_level_int = getattr(logging, log_level.upper(), logging.INFO)
        logging.basicConfig(level=self.log_level_int)
        
        self.executor = ThreadPoolExecutor(max_workers=4)
        
        self.logger.info("High-Quality Audio Extraction Service initialized.")

    def _save_to_downloads(self, audio_bytes: bytes, filename: str) -> str:
        """Save audio bytes to downloads folder and return file path"""
        file_path = os.path.join(self.downloads_dir, filename)
        with open(file_path, 'wb') as f:
            f.write(audio_bytes)
        self.logger.info(f"Saved extracted audio to: {file_path}")
        return file_path

    async def extract_high_quality_audio(self, url: str) -> Tuple[bytes, str]:
        """Extract high-quality audio with minimal processing for fingerprinting"""
        task_id = str(uuid.uuid4())[:8]
        download_dir = os.path.join(self.temp_dir_base, task_id)
        
        try:
            self.logger.info(f"Starting high-quality audio extraction for: {url}")
            
            # Step 1: Download highest quality audio
            audio_file = await self._download_high_quality_audio(url, download_dir)
            
            # Step 2: Apply minimal processing
            processed_audio = await self._apply_minimal_processing(audio_file, task_id)
            
            # Step 3: Read processed audio
            with open(processed_audio, 'rb') as f:
                audio_bytes = f.read()
            
            filename = f"extracted_hq_{task_id}.wav"
            
            # Save to downloads folder for testing purposes
            # self._save_to_downloads(audio_bytes, filename)
            
            return audio_bytes, filename
            
        finally:
            # Cleanup temporary files
            if os.path.exists(download_dir):
                shutil.rmtree(download_dir, ignore_errors=True)

    async def _download_high_quality_audio(self, url: str, download_dir: str) -> str:
        """Download highest quality audio available"""
        def _download():
            os.makedirs(download_dir, exist_ok=True)
            
            # High-quality yt-dlp configuration
            ydl_opts = {
                # Format selection - prioritize quality
                'format': 'bestaudio[ext=m4a]/bestaudio[ext=mp3]/bestaudio/best',
                'outtmpl': os.path.join(download_dir, 'audio.%(ext)s'),
                
                # Audio post-processing
                'postprocessors': [{
                    'key': 'FFmpegExtractAudio',
                    'preferredcodec': 'wav',
                    'preferredquality': '0',  # Best quality
                }],
                
                # Quality settings
                'audioquality': 0,  # Best quality
                'audio_format': 'best',
                
                # Network settings for reliability
                'retries': 3,
                'fragment_retries': 3,
                'socket_timeout': 30,
                'user_agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36',
                
                # Output control
                'quiet': True,
                'no_warnings': True,
                'extract_flat': False,
                
                # Additional options for better compatibility
                'ignoreerrors': False,
                'abort_on_unavailable_fragment': False,
            }
            
            try:
                with self.yt_dlp.YoutubeDL(ydl_opts) as ydl:
                    # Extract info first to check availability
                    info = ydl.extract_info(url, download=False)
                    self.logger.info(f"Found audio: {info.get('title', 'Unknown')} - Duration: {info.get('duration', 'Unknown')}s")
                    
                    # Download the audio
                    ydl.download([url])
                    
                    # Find the downloaded file
                    audio_file = os.path.join(download_dir, 'audio.wav')
                    if os.path.exists(audio_file):
                        return audio_file
                    
                    # Fallback: look for any audio file in the directory
                    for file in os.listdir(download_dir):
                        if file.startswith('audio.') and file.endswith(('.wav', '.mp3', '.m4a', '.aac')):
                            return os.path.join(download_dir, file)
                    
                    raise FileNotFoundError("Downloaded audio file not found")
                    
            except Exception as e:
                self.logger.warning(f"Primary download failed: {e}, trying fallback...")
                
                # Fallback with more permissive settings
                fallback_opts = {
                    'format': 'best[height<=720]/best',  # Lower quality video as last resort
                    'outtmpl': os.path.join(download_dir, 'fallback.%(ext)s'),
                    'postprocessors': [{
                        'key': 'FFmpegExtractAudio',
                        'preferredcodec': 'wav',
                        'preferredquality': '192',
                    }],
                    'quiet': True,
                    'no_warnings': True,
                }
                
                with self.yt_dlp.YoutubeDL(fallback_opts) as ydl_fallback:
                    ydl_fallback.download([url])
                    
                    fallback_file = os.path.join(download_dir, 'fallback.wav')
                    if os.path.exists(fallback_file):
                        return fallback_file
                    
                    raise RuntimeError(f"Failed to download audio from {url}: {e}")
        
        return await asyncio.get_event_loop().run_in_executor(self.executor, _download)

    async def _apply_minimal_processing(self, audio_file: str, task_id: str) -> str:
        """Apply minimal processing to optimize for fingerprinting"""
        def _process():
            output_file = os.path.join(self.temp_dir_base, task_id, "processed.wav")
            
            try:
                if self.has_librosa:
                    # Use librosa for high-quality processing
                    return self._process_with_librosa(audio_file, output_file)
                elif self.has_pydub:
                    # Use pydub as fallback
                    return self._process_with_pydub(audio_file, output_file)
                else:
                    # Use FFmpeg directly as last resort
                    return self._process_with_ffmpeg(audio_file, output_file)
                    
            except Exception as e:
                self.logger.warning(f"Processing failed: {e}, using original file")
                # If processing fails, just copy the original
                shutil.copy2(audio_file, output_file)
                return output_file
        
        return await asyncio.get_event_loop().run_in_executor(self.executor, _process)

    def _process_with_librosa(self, input_file: str, output_file: str) -> str:
        """Process audio using librosa for optimal quality"""
        import numpy as np
        
        # Load audio with original sample rate
        y, sr = self.librosa.load(input_file, sr=None, mono=False)
        
        # Ensure we have the right dimensions
        if y.ndim == 1:
            y = y.reshape(1, -1)  # Make it 2D for consistent processing
        
        # Apply minimal processing
        processed_channels = []
        for channel in range(y.shape[0]):
            channel_data = y[channel] if y.ndim > 1 else y
            
            # 1. DC offset removal
            channel_data = channel_data - np.mean(channel_data)
            
            # 2. Gentle high-pass filter to remove sub-audible content
            if sr > 40000:  # Only if sample rate is high enough
                channel_data = self.librosa.effects.preemphasis(channel_data, coef=0.95)
            
            # 3. Peak normalization (preserve dynamics)
            peak = np.max(np.abs(channel_data))
            if peak > 0:
                channel_data = channel_data * (0.95 / peak)  # Normalize to 95% to prevent clipping
            
            processed_channels.append(channel_data)
        
        # Combine channels
        if len(processed_channels) == 1:
            final_audio = processed_channels[0]
        else:
            final_audio = np.array(processed_channels)
        
        # Save with high quality
        self.sf.write(output_file, final_audio.T if final_audio.ndim > 1 else final_audio, sr, subtype='PCM_16')
        
        self.logger.info(f"Processed with librosa: SR={sr}Hz, Channels={final_audio.shape[0] if final_audio.ndim > 1 else 1}")
        return output_file

    def _process_with_pydub(self, input_file: str, output_file: str) -> str:
        """Process audio using pydub"""
        # Load audio
        audio = self.AudioSegment.from_file(input_file)
        
        # Apply minimal processing
        # 1. Normalize volume (preserve dynamics)
        target_dBFS = -3.0  # Leave some headroom
        current_dBFS = audio.dBFS
        if current_dBFS < target_dBFS - 10:  # Only normalize if significantly quiet
            change_in_dBFS = target_dBFS - current_dBFS
            audio = audio.apply_gain(change_in_dBFS)
        
        # 2. High-pass filter (remove very low frequencies)
        # This is basic - pydub doesn't have sophisticated filtering
        if audio.frame_rate > 40000:
            # Simple high-pass by reducing very low frequencies
            audio = audio.high_pass_filter(20)
        
        # Export with high quality
        audio.export(output_file, format="wav", parameters=["-acodec", "pcm_s16le"])
        
        self.logger.info(f"Processed with pydub: SR={audio.frame_rate}Hz, Channels={audio.channels}")
        return output_file

    def _process_with_ffmpeg(self, input_file: str, output_file: str) -> str:
        """Process audio using FFmpeg directly"""
        import subprocess
        
        # Build FFmpeg command for minimal processing
        cmd = [
            'ffmpeg', '-i', input_file,
            '-af', 'highpass=f=20,loudnorm=I=-16:TP=-1.5:LRA=11',  # Light processing
            '-acodec', 'pcm_s16le',
            '-ar', '44100',  # Standard sample rate for fingerprinting
            '-y',  # Overwrite output
            output_file
        ]
        
        try:
            subprocess.run(cmd, check=True, capture_output=True)
            self.logger.info("Processed with FFmpeg")
            return output_file
        except subprocess.CalledProcessError as e:
            self.logger.error(f"FFmpeg processing failed: {e}")
            # If FFmpeg fails, just copy the original
            shutil.copy2(input_file, output_file)
            return output_file


# --- FastAPI Application ---

audio_service: Optional[AudioExtractionService] = None

@asynccontextmanager
async def lifespan(app: FastAPI):
    global audio_service
    logging.info("API Lifespan: Startup.")
    
    try:
        audio_service = AudioExtractionService(log_level="INFO")
        logging.info("Audio Extraction Service initialized successfully.")
    except Exception as e:
        logging.critical(f"Fatal error during startup: {e}", exc_info=True)
        audio_service = None
    yield
    
    gc.collect()
    logging.info("API shutdown completed.")

app = FastAPI(
    title="High-Quality Audio Extraction API", 
    version="5.0.0-minimal", 
    description="Optimized for audio fingerprinting with minimal processing",
    lifespan=lifespan
)

app.add_middleware(
    CORSMiddleware, 
    allow_origins=["*"], 
    allow_methods=["*"], 
    allow_headers=["*"], 
    allow_credentials=True
)

@app.get("/", response_model=HealthResponse)
async def root():
    processing_capabilities = []
    if audio_service:
        if audio_service.has_librosa:
            processing_capabilities.append("librosa")
        if audio_service.has_pydub:
            processing_capabilities.append("pydub")
        processing_capabilities.append("ffmpeg")
    
    return HealthResponse(
        status="healthy" if audio_service else "degraded", 
        version=app.version,
        dependencies={
            "service_available": audio_service is not None,
            "processing_capabilities": processing_capabilities,
            "yt_dlp_available": hasattr(audio_service, 'yt_dlp') if audio_service else False
        }
    )

@app.post("/remove-speech-preserve-vocals", tags=["Audio Extraction"])
async def remove_speech_preserve_vocals(request: ExtractAudioRequest):
    """
    Extract high-quality audio optimized for fingerprinting.
    
    Note: This endpoint now performs minimal processing to preserve 
    original audio characteristics for optimal fingerprinting accuracy.
    """
    if not audio_service:
        raise HTTPException(status_code=503, detail="Audio extraction service not available.")
    
    try:
        logging.info(f"Processing audio extraction request for: {request.url}")
        
        audio_bytes, filename = await audio_service.extract_high_quality_audio(str(request.url))
        
        return StreamingResponse(
            io.BytesIO(audio_bytes), 
            media_type="audio/wav",
            headers={
                "Content-Disposition": f"attachment; filename={filename}",
                "X-Processing-Method": "minimal-hq-extraction",
                "X-Optimized-For": "fingerprinting"
            }
        )
        
    except Exception as e:
        logging.error(f"Error processing audio extraction request: {e}", exc_info=True)
        raise HTTPException(
            status_code=500, 
            detail=f"Failed to extract audio: {str(e)}"
        )