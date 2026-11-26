const bands = 16;

export class StudioAudio {
  async connect() {
    if (!this.context) {
      this.context = new AudioContext();
      this.gain = this.context.createGain();
      this.gain.connect(this.context.destination);
      this.frequency = this.analyser(2048);
      this.channels = [this.analyser(512), this.analyser(512)];
      this.split = this.context.createChannelSplitter(2);
      this.channels.forEach(({ node }, i) => this.split.connect(node, i));
    }
    await this.context.resume();
  }

  analyser(size) {
    const node = this.context.createAnalyser();
    node.fftSize = size;
    node.minDecibels = -80;
    node.maxDecibels = -20;
    return { node, data: new Uint8Array(size), wave: new Float32Array(size) };
  }

  async play(buffer, loop, current) {
    await this.connect();
    if (!current()) return;
    this.stop();
    this.buffer = buffer;
    this.loop = loop;
    this.offset = 0;
    this.resume();
  }

  async playFile(file, current) {
    await this.connect();
    const buffer = await this.context.decodeAudioData(await file.arrayBuffer());
    await this.play(buffer, false, current);
  }

  async demo(current) {
    await this.connect();
    const rate = 22050;
    const buffer = this.context.createBuffer(2, rate * 16, rate);
    const notes = [55, 55, 82.4, 65.4, 49, 49, 73.4, 65.4];
    for (let channel = 0; channel < 2; channel++) {
      const wave = buffer.getChannelData(channel);
      for (let i = 0; i < wave.length; i++) {
        const t = i / rate;
        const step = t % .25;
        const beat = t % .5;
        const note = notes[Math.floor(t * 4) % notes.length];
        const bass = (.18 * Math.sin(2 * Math.PI * note * t) + .07 * Math.sin(6 * Math.PI * note * t)) * (1 - step * 3);
        const kick = .38 * Math.sin(2 * Math.PI * (52 * beat + 3 * (1 - Math.exp(-40 * beat)))) * Math.exp(-18 * beat);
        const arp = .10 * Math.sin(2 * Math.PI * note * (8 + channel * 2) * t) * Math.exp(-7 * step);
        wave[i] = (bass + kick + arp) * Math.min(1, t * 100, (16 - t) * 100);
      }
    }
    await this.play(buffer, true, current);
  }

  stop() {
    if (!this.source) return;
    this.source.stop();
    this.source.disconnect();
    this.source = null;
  }

  get playing() { return Boolean(this.source); }
  get duration() { return this.buffer?.duration || 0; }
  get position() {
    if (!this.source) return this.offset || 0;
    const elapsed = this.offset + this.context.currentTime - this.started;
    return this.loop ? elapsed % this.duration : Math.min(elapsed, this.duration);
  }

  pause() {
    this.offset = this.position;
    this.stop();
  }

  resume() {
    if (!this.buffer || this.source) return;
    if (this.offset >= this.duration) this.offset = 0;
    const source = this.context.createBufferSource();
    source.buffer = this.buffer;
    source.loop = this.loop;
    source.connect(this.gain);
    source.connect(this.frequency.node);
    source.connect(this.split);
    source.onended = () => {
      if (this.source !== source) return;
      this.offset = 0;
      this.source = null;
      source.disconnect();
      this.onend?.();
    };
    this.started = this.context.currentTime;
    this.source = source;
    source.start(0, this.offset);
  }

  volume(value) {
    if (this.gain) this.gain.gain.value = value / 100;
  }

  levels() {
    if (!this.playing) return undefined;
    const { node, data } = this.frequency;
    node.getByteFrequencyData(data);
    const hz = this.context.sampleRate / node.fftSize;
    const strength = (from, to) => {
      let maximum = 0;
      for (let i = from; i < Math.max(from + 1, to); i++) maximum = Math.max(maximum, data[i]);
      return maximum / 255;
    };
    const spectrum = Array.from({ length: bands }, (_, n) => {
      const start = Math.max(1, Math.floor(35 * 450 ** (n / bands) / hz));
      const end = Math.min(node.frequencyBinCount, Math.ceil(35 * 450 ** ((n + 1) / bands) / hz));
      return strength(start, end);
    });
    const vu = this.channels.map(({ node, wave }) => {
      node.getFloatTimeDomainData(wave);
      const rms = Math.sqrt(wave.reduce((sum, value) => sum + value * value, 0) / wave.length);
      return Math.min(1, Math.sqrt(rms) * 1.8);
    });
    if (this.buffer.numberOfChannels === 1) vu[1] = vu[0];
    return { bands: spectrum, bass: strength(1, Math.ceil(140 / hz)), vu };
  }
}
