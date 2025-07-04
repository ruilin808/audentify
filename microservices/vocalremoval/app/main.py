#!/usr/bin/env python3
"""
Enhanced Audio Separation API - Speech-Only Removal
Memory-optimized version that handles CUDA memory constraints
"""

import os
# Set memory optimization BEFORE any torch imports
os.environ["PYTORCH_CUDA_ALLOC_CONF"] = "expandable_segments:True"

import uuid
import asyncio
import logging
import io
import tempfile
import shutil
import gc
import torch
from pathlib import Path
from typing import Dict, Optional, Any, Tuple, List
from contextlib import asynccontextmanager
from concurrent.futures import ThreadPoolExecutor

import requests
from fastapi import FastAPI, HTTPException
from fastapi.responses import StreamingResponse
from fastapi.middleware.cors import CORSMiddleware
from tqdm import tqdm

# Import your existing models
from app.models import ProcessUrlRequest, HealthResponse

# --- Enhanced Model Registry ---
MODEL_REGISTRY = {
    "UVR-DeEcho-DeReverb.pth": {
        "url": "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/UVR-DeEcho-DeReverb.pth",
        "primary_stem": "DeEcho",
        "model_type": "mdx_net",
        "description": "Removes echo and reverb, good for speech cleanup"
    },
    "5_HP-Karaoke-UVR.pth": {
        "url": "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/5_HP-Karaoke-UVR.pth",
        "primary_stem": "Vocals",
        "model_type": "mdx_net",
        "description": "High-quality vocal isolation"
    },
    "Kim_Vocal_2.onnx": {
        "url": "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/Kim_Vocal_2.onnx",
        "primary_stem": "Vocals",
        "model_type": "mdx_net",
        "description": "Kim's vocal model - good for speech vs singing distinction"
    },
    "3_HP-Vocal-UVR.pth": {
        "url": "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/3_HP-Vocal-UVR.pth",
        "primary_stem": "Vocals",
        "model_type": "vr_arch",
        "description": "High-performance vocal separation"
    }
}

class EnhancedAudioSeparatorService:
    _model_cache: Dict[str, Any] = {}
    _model_lock = asyncio.Lock()
    
    def __init__(self, log_level: str = "INFO"):
        try:
            from audio_separator.separator import Separator
            self.Separator = Separator
            import yt_dlp
            self.yt_dlp = yt_dlp
            import soundfile as sf
            self.sf = sf
        except ImportError as e:
            raise ImportError(f"Required library not found! Error: {e}")
        
        self.temp_dir_base = os.path.join(os.getcwd(), "temp")
        self.model_dir = os.path.join(self.temp_dir_base, "models")
        # Add downloads directory
        self.downloads_dir = os.path.join(os.getcwd(), "downloads")
        os.makedirs(self.model_dir, exist_ok=True)
        os.makedirs(self.downloads_dir, exist_ok=True)
        
        self.logger = logging.getLogger(__name__)
        self.log_level_int = getattr(logging, log_level.upper(), logging.INFO)
        logging.basicConfig(level=self.log_level_int)
        
        self.executor = ThreadPoolExecutor(max_workers=4)
        self.memory_threshold_gb = 6.0
        self.check_memory_optimization()
        
        self.logger.info("Enhanced Audio Separator Service initialized.")

    def check_memory_optimization(self):
        """Check if memory optimization is needed"""
        if torch.cuda.is_available():
            device_props = torch.cuda.get_device_properties(0)
            total_memory_gb = device_props.total_memory / 1024**3
            allocated_gb = torch.cuda.memory_allocated() / 1024**3
            available_gb = total_memory_gb - allocated_gb
            
            self.use_memory_optimization = available_gb < self.memory_threshold_gb
            
            self.logger.info(f"GPU Memory: Total={total_memory_gb:.1f}GB, Available={available_gb:.1f}GB")
            self.logger.info(f"Memory Optimization: {'ENABLED' if self.use_memory_optimization else 'DISABLED'}")
        else:
            self.use_memory_optimization = False
            self.logger.info("CUDA not available - using CPU mode")

    def get_available_memory_gb(self):
        """Get current available GPU memory"""
        if torch.cuda.is_available():
            device_props = torch.cuda.get_device_properties(0)
            total_memory_gb = device_props.total_memory / 1024**3
            allocated_gb = torch.cuda.memory_allocated() / 1024**3
            return total_memory_gb - allocated_gb
        return float('inf')

    def _clear_gpu_memory(self):
        """Clear GPU memory"""
        if torch.cuda.is_available():
            torch.cuda.empty_cache()
            torch.cuda.ipc_collect()
            torch.cuda.synchronize()
        gc.collect()
        self.logger.info("GPU memory cleared")

    def _force_garbage_collection(self):
        """Force comprehensive garbage collection"""
        gc.collect()
        if torch.cuda.is_available():
            torch.cuda.empty_cache()
            torch.cuda.ipc_collect()
            for i in range(torch.cuda.device_count()):
                with torch.cuda.device(i):
                    torch.cuda.empty_cache()
        for _ in range(3):
            gc.collect()

    def _log_gpu_memory(self, step: str):
        """Log GPU memory usage"""
        if torch.cuda.is_available():
            allocated = torch.cuda.memory_allocated() / 1024**3
            cached = torch.cuda.memory_reserved() / 1024**3
            self.logger.info(f"{step} - GPU Memory: {allocated:.2f}GB allocated, {cached:.2f}GB cached")

    async def _ensure_model_exists(self, model_name: str, url: str):
        """Download model if needed"""
        model_path = os.path.join(self.model_dir, model_name)
        if os.path.exists(model_path):
            return

        def _download():
            try:
                with requests.get(url, stream=True, timeout=30) as r:
                    r.raise_for_status()
                    total_size = int(r.headers.get('content-length', 0))
                    with open(model_path, 'wb') as f, tqdm(
                        total=total_size, unit='iB', unit_scale=True, desc=f"Downloading {model_name}"
                    ) as bar:
                        for chunk in r.iter_content(chunk_size=8192):
                            f.write(chunk)
                            bar.update(len(chunk))
                self.logger.info(f"Model '{model_name}' downloaded successfully.")
            except Exception as e:
                if os.path.exists(model_path):
                    os.remove(model_path)
                raise RuntimeError(f"Failed to download {model_name}: {e}")

        await asyncio.get_event_loop().run_in_executor(self.executor, _download)

    async def get_or_load_model(self, model_name: str, force_reload: bool = False):
        """Load model with dynamic memory management"""
        available_memory = self.get_available_memory_gb()
        needs_optimization = available_memory < self.memory_threshold_gb
        
        if needs_optimization and not self.use_memory_optimization:
            self.logger.warning(f"Available memory ({available_memory:.1f}GB) below threshold")
            self.use_memory_optimization = True
        
        if self.use_memory_optimization or needs_optimization:
            return await self._get_model_memory_optimized(model_name, force_reload)
        else:
            return await self._get_model_normal(model_name, force_reload)

    async def _get_model_memory_optimized(self, model_name: str, force_reload: bool = False):
        """Memory-optimized model loading"""
        if len(self._model_cache) > 0:
            if model_name not in self._model_cache or force_reload:
                self.logger.info("Clearing all cached models...")
                self._model_cache.clear()
                self._force_garbage_collection()

        if not force_reload and model_name in self._model_cache:
            return self._model_cache[model_name]

        async with self._model_lock:
            if not force_reload and model_name in self._model_cache:
                return self._model_cache[model_name]

            model_info = MODEL_REGISTRY.get(model_name)
            if not model_info:
                raise ValueError(f"Model '{model_name}' not found in registry")
            
            await self._ensure_model_exists(model_name, model_info["url"])

            def _load():
                separator = self.Separator(
                    log_level=self.log_level_int, 
                    model_file_dir=self.model_dir
                )
                separator.load_model(model_filename=model_name)
                return separator
            
            self.logger.info(f"Loading model '{model_name}' (optimized)")
            separator_instance = await asyncio.get_event_loop().run_in_executor(self.executor, _load)
            
            self._model_cache = {model_name: separator_instance}
            self.logger.info(f"Model '{model_name}' loaded")
            
            return separator_instance

    async def _get_model_normal(self, model_name: str, force_reload: bool = False):
        """Normal model loading with caching"""
        if not force_reload and model_name in self._model_cache:
            return self._model_cache[model_name]

        async with self._model_lock:
            if not force_reload and model_name in self._model_cache:
                return self._model_cache[model_name]

            self._clear_gpu_memory()

            model_info = MODEL_REGISTRY.get(model_name)
            if not model_info:
                raise ValueError(f"Model '{model_name}' not found in registry")
            
            await self._ensure_model_exists(model_name, model_info["url"])

            def _load():
                separator = self.Separator(
                    log_level=self.log_level_int, 
                    model_file_dir=self.model_dir
                )
                separator.load_model(model_filename=model_name)
                return separator
            
            self.logger.info(f"Loading model '{model_name}' (normal)")
            separator_instance = await asyncio.get_event_loop().run_in_executor(self.executor, _load)
            
            self._model_cache[model_name] = separator_instance
            self.logger.info(f"Model '{model_name}' loaded. Cache size: {len(self._model_cache)}")
            
            return separator_instance

    def _save_to_downloads(self, wav_bytes: bytes, filename: str) -> str:
        """Save audio bytes to downloads folder and return file path"""
        file_path = os.path.join(self.downloads_dir, filename)
        with open(file_path, 'wb') as f:
            f.write(wav_bytes)
        self.logger.info(f"Saved processed audio to: {file_path}")
        return file_path

    async def process_url_advanced(self, url: str, method: str = "preserve_vocals") -> Tuple[bytes, str]:
        """Process URL with memory management"""
        task_id = str(uuid.uuid4())[:8]
        download_dir = os.path.join(self.temp_dir_base, task_id)
        
        try:
            available_memory = self.get_available_memory_gb()
            self.logger.info(f"Available GPU memory: {available_memory:.1f}GB")
            
            if available_memory < self.memory_threshold_gb:
                self._force_garbage_collection()
            else:
                self._clear_gpu_memory()
            
            audio_file = await self._download_audio(url, download_dir)
            
            if method == "preserve_vocals":
                if self.use_memory_optimization or available_memory < self.memory_threshold_gb:
                    self.logger.info("Using memory-optimized vocal preservation")
                    wav_bytes = await self._preserve_vocals_optimized(audio_file, task_id)
                else:
                    self.logger.info("Using full-quality vocal preservation")
                    wav_bytes = await self._preserve_vocals_full(audio_file, task_id)
                filename = f"vocals_preserved_{task_id}.wav"
            elif method == "instrumental_only":
                wav_bytes = await self._extract_instrumental(audio_file, task_id)
                filename = f"instrumental_{task_id}.wav"
            else:
                wav_bytes = await self._simple_speech_removal(audio_file, task_id)
                filename = f"speech_removed_{task_id}.wav"
            
            # Save to downloads folder
            self._save_to_downloads(wav_bytes, filename)
            
            return wav_bytes, filename
            
        finally:
            if self.use_memory_optimization:
                self._force_garbage_collection()
            else:
                self._clear_gpu_memory()
            if os.path.exists(download_dir):
                shutil.rmtree(download_dir, ignore_errors=True)

    async def _preserve_vocals_full(self, audio_file: str, task_id: str) -> bytes:
        """Full-quality vocal preservation (4-model pipeline)"""
        self.logger.info("Starting full-quality vocal preservation...")
        
        try:
            # Step 1: Separate vocals from instrumentals
            vocals_separator = await self.get_or_load_model("5_HP-Karaoke-UVR.pth")
            vocals_file, instrumental_file = await self._separate_vocals_instrumental(
                vocals_separator, audio_file, task_id
            )
            
            # Step 2: Separate speech from singing in vocals
            kim_separator = await self.get_or_load_model("Kim_Vocal_2.onnx")
            singing_vocals, _ = await self._separate_singing_speech(
                kim_separator, vocals_file, task_id
            )
            
            # Step 3: Combine singing with instrumental
            final_audio = await self._combine_audio_files(
                [singing_vocals, instrumental_file], task_id
            )
            
            # Step 4: Apply cleanup
            deecho_separator = await self.get_or_load_model("UVR-DeEcho-DeReverb.pth")
            cleaned_audio = await self._apply_cleanup(deecho_separator, final_audio, task_id)
            
            with open(cleaned_audio, 'rb') as f:
                return f.read()
                
        except Exception as e:
            self.logger.error(f"Error in full pipeline: {e}")
            
            if "CUDA" in str(e) and "memory" in str(e).lower():
                self.logger.warning("Memory error, falling back to optimized method")
                self.use_memory_optimization = True
                return await self._preserve_vocals_optimized(audio_file, task_id)
            else:
                raise

    async def _preserve_vocals_optimized(self, audio_file: str, task_id: str) -> bytes:
        """Memory-optimized vocal preservation (single model)"""
        self.logger.info("Starting optimized vocal preservation...")
        
        try:
            if self.use_memory_optimization:
                self._force_garbage_collection()
            
            kim_separator = await self.get_or_load_model("Kim_Vocal_2.onnx", force_reload=self.use_memory_optimization)
            
            def _separate():
                with tempfile.TemporaryDirectory() as temp_output_dir:
                    kim_separator.output_dir = temp_output_dir
                    output_files = kim_separator.separate(audio_file)
                    
                    target_file = None
                    for f in output_files:
                        file_name = Path(f).name
                        if '(Instrumental)' in file_name or '(Music)' in file_name:
                            target_file = f
                            break
                    
                    if not target_file:
                        target_file = output_files[0] if output_files else None
                    
                    if not target_file:
                        raise RuntimeError("Could not generate audio")
                    
                    with open(target_file, 'rb') as f:
                        return f.read()
            
            result = await asyncio.get_event_loop().run_in_executor(self.executor, _separate)
            
            if self.use_memory_optimization:
                self._force_garbage_collection()
            
            return result
                
        except Exception as e:
            self.logger.error(f"Error in optimized preservation: {e}")
            if self.use_memory_optimization:
                self._force_garbage_collection()
            
            if "CUDA" in str(e) and "memory" in str(e).lower():
                return await self._simple_speech_removal(audio_file, task_id)
            else:
                raise

    async def _extract_instrumental_with_separator(self, separator, audio_file: str, task_id: str) -> bytes:
        """Extract instrumental using separator"""
        def _separate():
            with tempfile.TemporaryDirectory() as temp_output_dir:
                separator.output_dir = temp_output_dir
                output_files = separator.separate(audio_file)
                target_file = next((f for f in output_files if '(Instrumental)' in Path(f).name), output_files[0])
                with open(target_file, 'rb') as f:
                    return f.read()
        
        result = await asyncio.get_event_loop().run_in_executor(self.executor, _separate)
        if self.use_memory_optimization:
            self._force_garbage_collection()
        return result

    async def _separate_vocals_instrumental(self, separator, audio_file: str, task_id: str) -> Tuple[str, str]:
        """Separate vocals from instrumental"""
        def _separate():
            with tempfile.TemporaryDirectory() as temp_output_dir:
                separator.output_dir = temp_output_dir
                output_files = separator.separate(audio_file)
                
                vocals_file = next((f for f in output_files if '(Vocals)' in Path(f).name), None)
                instrumental_file = next((f for f in output_files if '(Instrumental)' in Path(f).name), None)
                
                if not vocals_file or not instrumental_file:
                    raise RuntimeError("Could not separate vocals and instrumental")
                
                task_dir = os.path.join(self.temp_dir_base, task_id)
                os.makedirs(task_dir, exist_ok=True)
                
                vocals_dest = os.path.join(task_dir, "vocals.wav")
                instrumental_dest = os.path.join(task_dir, "instrumental.wav")
                
                shutil.copy2(vocals_file, vocals_dest)
                shutil.copy2(instrumental_file, instrumental_dest)
                
                return vocals_dest, instrumental_dest
        
        return await asyncio.get_event_loop().run_in_executor(self.executor, _separate)

    async def _separate_singing_speech(self, separator, vocals_file: str, task_id: str) -> Tuple[str, str]:
        """Separate singing from speech"""
        def _separate():
            with tempfile.TemporaryDirectory() as temp_output_dir:
                separator.output_dir = temp_output_dir
                output_files = separator.separate(vocals_file)
                
                primary_file = next((f for f in output_files if '(Vocals)' in Path(f).name), output_files[0])
                secondary_file = next((f for f in output_files if '(Instrumental)' in Path(f).name), output_files[1] if len(output_files) > 1 else None)
                
                task_dir = os.path.join(self.temp_dir_base, task_id)
                
                singing_dest = os.path.join(task_dir, "singing.wav")
                speech_dest = os.path.join(task_dir, "speech.wav")
                
                shutil.copy2(primary_file, singing_dest)
                if secondary_file:
                    shutil.copy2(secondary_file, speech_dest)
                
                return singing_dest, speech_dest
        
        return await asyncio.get_event_loop().run_in_executor(self.executor, _separate)

    async def _combine_audio_files(self, audio_files: List[str], task_id: str) -> str:
        """Combine multiple audio files"""
        def _combine():
            import soundfile as sf
            import numpy as np
            
            combined_audio = None
            sample_rate = None
            
            for audio_file in audio_files:
                if os.path.exists(audio_file):
                    audio, sr = sf.read(audio_file)
                    
                    if combined_audio is None:
                        combined_audio = audio
                        sample_rate = sr
                    else:
                        if sr != sample_rate:
                            audio = np.interp(
                                np.linspace(0, len(audio), int(len(audio) * sample_rate / sr)),
                                np.arange(len(audio)),
                                audio
                            )
                        
                        min_len = min(len(combined_audio), len(audio))
                        combined_audio = combined_audio[:min_len] + audio[:min_len]
            
            task_dir = os.path.join(self.temp_dir_base, task_id)
            combined_file = os.path.join(task_dir, "combined.wav")
            sf.write(combined_file, combined_audio, sample_rate)
            
            return combined_file
        
        return await asyncio.get_event_loop().run_in_executor(self.executor, _combine)

    async def _apply_cleanup(self, separator, audio_file: str, task_id: str) -> str:
        """Apply cleanup"""
        def _cleanup():
            with tempfile.TemporaryDirectory() as temp_output_dir:
                separator.output_dir = temp_output_dir
                output_files = separator.separate(audio_file)
                
                cleaned_file = next((f for f in output_files if '(DeEcho)' in Path(f).name), output_files[0])
                
                task_dir = os.path.join(self.temp_dir_base, task_id)
                final_dest = os.path.join(task_dir, "final_cleaned.wav")
                shutil.copy2(cleaned_file, final_dest)
                
                return final_dest
        
        return await asyncio.get_event_loop().run_in_executor(self.executor, _cleanup)

    async def _extract_instrumental(self, audio_file: str, task_id: str) -> bytes:
        """Extract instrumental"""
        if self.use_memory_optimization:
            self._force_garbage_collection()
            separator = await self.get_or_load_model("3_HP-Vocal-UVR.pth", force_reload=True)
        else:
            separator = await self.get_or_load_model("5_HP-Karaoke-UVR.pth")
        return await self._extract_instrumental_with_separator(separator, audio_file, task_id)

    async def _simple_speech_removal(self, audio_file: str, task_id: str) -> bytes:
        """Simple speech removal"""
        if self.use_memory_optimization:
            self._force_garbage_collection()
            separator = await self.get_or_load_model("3_HP-Vocal-UVR.pth", force_reload=True)
        else:
            separator = await self.get_or_load_model("3_HP-Vocal-UVR.pth")
        return await self._extract_instrumental_with_separator(separator, audio_file, task_id)

    async def _download_audio(self, url: str, download_dir: str) -> str:
        """Download audio from URL"""
        def _download():
            os.makedirs(download_dir, exist_ok=True)
            ydl_opts = {
                'format': 'bestaudio/best',
                'outtmpl': os.path.join(download_dir, 'audio.%(ext)s'),
                'postprocessors': [{'key': 'FFmpegExtractAudio', 'preferredcodec': 'wav'}],
                'quiet': True,
                'no_warnings': True,
                'user_agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
            }
            with self.yt_dlp.YoutubeDL(ydl_opts) as ydl:
                ydl.download([url])
            
            path = os.path.join(download_dir, 'audio.wav')
            if os.path.exists(path):
                return path
            raise FileNotFoundError("Downloaded audio file not found.")
        
        return await asyncio.get_event_loop().run_in_executor(self.executor, _download)

# --- FastAPI Application ---

separator_service: Optional[EnhancedAudioSeparatorService] = None

@asynccontextmanager
async def lifespan(app: FastAPI):
    global separator_service
    logging.info("API Lifespan: Startup.")
    
    if torch.cuda.is_available():
        device_props = torch.cuda.get_device_properties(0)
        total_memory_gb = device_props.total_memory / 1024**3
        
        if total_memory_gb >= 10:
            memory_fraction = 0.85
        elif total_memory_gb >= 8:
            memory_fraction = 0.75
        else:
            memory_fraction = 0.7
        
        torch.cuda.set_per_process_memory_fraction(memory_fraction)
        torch.cuda.empty_cache()
        
        logging.info(f"GPU configured: {total_memory_gb:.1f}GB total, {memory_fraction*100:.0f}% allocation")
    
    try:
        separator_service = EnhancedAudioSeparatorService(log_level="INFO")
        logging.info("Service initialized with dynamic memory management.")
    except Exception as e:
        logging.critical(f"Fatal error during startup: {e}", exc_info=True)
        separator_service = None
    yield
    
    if torch.cuda.is_available():
        torch.cuda.empty_cache()
    gc.collect()
    logging.info("API shutdown completed.")

app = FastAPI(title="Enhanced Audio Separation API", version="4.0.0-enhanced", lifespan=lifespan)
app.add_middleware(CORSMiddleware, allow_origins=["*"], allow_methods=["*"], allow_headers=["*"], allow_credentials=True)

@app.get("/", response_model=HealthResponse)
async def root():
    return HealthResponse(
        status="healthy" if separator_service else "degraded", 
        version=app.version,
        dependencies={
            "models_loaded": len(separator_service._model_cache) if separator_service else 0,
            "cuda_available": torch.cuda.is_available(),
            "memory_optimization": separator_service.use_memory_optimization if separator_service else False
        }
    )

@app.post("/remove-speech-preserve-vocals", tags=["Audio Separation"])
async def remove_speech_preserve_vocals(request: ProcessUrlRequest):
    """Remove speech/commentary while preserving vocals and instrumentals"""
    if not separator_service:
        raise HTTPException(status_code=503, detail="Service not available.")
    try:
        wav_bytes, filename = await separator_service.process_url_advanced(
            str(request.url), method="preserve_vocals"
        )
        return StreamingResponse(
            io.BytesIO(wav_bytes), 
            media_type="audio/wav",
            headers={"Content-Disposition": f"attachment; filename={filename}"}
        )
    except Exception as e:
        logging.error(f"Error processing request: {e}", exc_info=True)
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/extract-instrumental", tags=["Audio Separation"])
async def extract_instrumental(request: ProcessUrlRequest):
    """Extract instrumental only"""
    if not separator_service:
        raise HTTPException(status_code=503, detail="Service not available.")
    try:
        wav_bytes, filename = await separator_service.process_url_advanced(
            str(request.url), method="instrumental_only"
        )
        return StreamingResponse(
            io.BytesIO(wav_bytes), 
            media_type="audio/wav",
            headers={"Content-Disposition": f"attachment; filename={filename}"}
        )
    except Exception as e:
        logging.error(f"Error processing request: {e}", exc_info=True)
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/remove-commentary", tags=["Audio Separation"])
async def remove_commentary(request: ProcessUrlRequest):
    """Simple speech removal"""
    if not separator_service:
        raise HTTPException(status_code=503, detail="Service not available.")
    try:
        wav_bytes, filename = await separator_service.process_url_advanced(
            str(request.url), method="simple"
        )
        return StreamingResponse(
            io.BytesIO(wav_bytes), 
            media_type="audio/wav",
            headers={"Content-Disposition": f"attachment; filename={filename}"}
        )
    except Exception as e:
        logging.error(f"Error processing request: {e}", exc_info=True)
        raise HTTPException(status_code=500, detail=str(e))