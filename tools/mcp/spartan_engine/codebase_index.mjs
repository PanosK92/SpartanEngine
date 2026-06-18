import fs from "node:fs/promises";
import path from "node:path";

const source_extensions = new Set([".h", ".hpp", ".cpp", ".c", ".mjs", ".js", ".lua", ".cs", ".md"]);
const skipped_directories = new Set([".git", ".vs", "binaries", "node_modules", "third_party"]);

function tokenize(text) {
  return String(text ?? "")
    .toLowerCase()
    .split(/[^a-z0-9_]+/g)
    .filter((term) => term.length >= 2);
}

function compact_text(text, max_length = 2200) {
  const value = String(text ?? "").replace(/\s+/g, " ").trim();
  if (value.length <= max_length) {
    return value;
  }

  return `${value.slice(0, max_length).trimEnd()}...`;
}

export class CodebaseIndex {
  constructor(project_root) {
    this.project_root = project_root;
    this.entries = [];
    this.file_stats = new Map();
    this.indexing = false;
    this.ready = false;
    this.files_processed = 0;
    this.files_total = 0;
    this.current_file = "";
    this.index_task = null;
  }

  status() {
    return {
      ready: this.ready,
      indexing: this.indexing,
      chunks: this.entries.length,
      files_processed: this.files_processed,
      files_total: this.files_total,
      current_file: this.current_file,
    };
  }

  async ensure() {
    if (this.ready) {
      return;
    }

    if (!this.index_task) {
      this.index_task = this.rebuild().finally(() => {
        if (!this.ready) {
          this.index_task = null;
        }
      });
    }

    await this.index_task;
  }

  async rebuild() {
    this.indexing = true;
    this.ready = false;
    this.entries = [];
    this.files_processed = 0;
    this.files_total = 0;
    this.current_file = "";

    try {
      const roots = [
        path.join(this.project_root, "source"),
        path.join(this.project_root, "tools", "mcp", "spartan_engine"),
      ];
      const files = [];
      for (const root of roots) {
        files.push(...await this.collect_files(root));
      }
      files.sort((a, b) => a.localeCompare(b));
      this.files_total = files.length;
      this.file_stats.clear();

      for (const file of files) {
        this.current_file = path.relative(this.project_root, file).replaceAll("\\", "/");
        this.files_processed++;

        let text = "";
        try {
          const stat = await fs.stat(file);
          text = await fs.readFile(file, "utf8");
          this.file_stats.set(this.current_file, {
            mtime_ms: stat.mtimeMs,
            size: stat.size,
          });
        } catch {
          continue;
        }

        this.add_file(file, text);
      }

      this.ready = true;
    } finally {
      this.current_file = "";
      this.indexing = false;
    }
  }

  async collect_files(directory) {
    const files = [];
    let entries = [];
    try {
      entries = await fs.readdir(directory, { withFileTypes: true });
    } catch {
      return files;
    }

    for (const entry of entries) {
      const full_path = path.join(directory, entry.name);
      if (entry.isDirectory()) {
        if (skipped_directories.has(entry.name)) {
          continue;
        }
        if (full_path.replaceAll("\\", "/").endsWith("source/editor/ImGui/Source")) {
          continue;
        }
        files.push(...await this.collect_files(full_path));
      } else if (entry.isFile() && source_extensions.has(path.extname(entry.name).toLowerCase())) {
        files.push(full_path);
      }
    }

    return files;
  }

  is_allowed_source_file(relative_path) {
    const normalized = String(relative_path ?? "").replaceAll("\\", "/");
    return (normalized.startsWith("source/") || normalized.startsWith("tools/mcp/spartan_engine/")) &&
      source_extensions.has(path.extname(normalized).toLowerCase()) &&
      !normalized.split("/").some((part) => skipped_directories.has(part)) &&
      !normalized.includes("source/editor/ImGui/Source/");
  }

  async refresh_changed() {
    if (!this.ready || this.indexing) {
      return;
    }

    this.indexing = true;
    this.files_processed = 0;
    this.files_total = 0;
    this.current_file = "";

    try {
      const roots = [
        path.join(this.project_root, "source"),
        path.join(this.project_root, "tools", "mcp", "spartan_engine"),
      ];
      const files = [];
      for (const root of roots) {
        files.push(...await this.collect_files(root));
      }
      files.sort((a, b) => a.localeCompare(b));
      this.files_total = files.length;

      const seen = new Set();
      for (const file of files) {
        const relative_path = path.relative(this.project_root, file).replaceAll("\\", "/");
        seen.add(relative_path);
        this.current_file = relative_path;
        this.files_processed++;

        let stat;
        try {
          stat = await fs.stat(file);
        } catch {
          continue;
        }

        const previous = this.file_stats.get(relative_path);
        if (previous && previous.mtime_ms === stat.mtimeMs && previous.size === stat.size) {
          continue;
        }

        let text = "";
        try {
          text = await fs.readFile(file, "utf8");
        } catch {
          continue;
        }

        this.entries = this.entries.filter((entry) => entry.path !== relative_path);
        this.add_file(file, text);
        this.file_stats.set(relative_path, {
          mtime_ms: stat.mtimeMs,
          size: stat.size,
        });
      }

      for (const relative_path of [...this.file_stats.keys()]) {
        if (!seen.has(relative_path)) {
          this.file_stats.delete(relative_path);
          this.entries = this.entries.filter((entry) => entry.path !== relative_path);
        }
      }
    } finally {
      this.current_file = "";
      this.indexing = false;
    }
  }

  add_file(file, text) {
    const relative_path = path.relative(this.project_root, file).replaceAll("\\", "/");
    const lines = text.split(/\r?\n/g);
    const chunk_size = 120;
    const stride = 90;

    for (let start = 0; start < lines.length; start += stride) {
      const chunk_lines = lines.slice(start, start + chunk_size);
      if (chunk_lines.length === 0) {
        continue;
      }

      const content = chunk_lines.join("\n").trim();
      if (!content) {
        continue;
      }

      const terms = tokenize(`${relative_path}\n${content}`);
      const term_counts = new Map();
      for (const term of terms) {
        term_counts.set(term, (term_counts.get(term) ?? 0) + 1);
      }

      this.entries.push({
        path: relative_path,
        start_line: start + 1,
        end_line: start + chunk_lines.length,
        content: compact_text(content),
        term_counts,
      });
    }
  }

  async search(query, top_k = 8) {
    await this.ensure();
    await this.refresh_changed();

    const terms = tokenize(query);
    if (terms.length === 0) {
      return [];
    }

    const results = [];
    for (const entry of this.entries) {
      let score = 0;
      for (const term of terms) {
        score += entry.term_counts.get(term) ?? 0;
        if (entry.path.toLowerCase().includes(term)) {
          score += 2;
        }
      }

      if (score > 0) {
        results.push({
          score,
          path: entry.path,
          start_line: entry.start_line,
          end_line: entry.end_line,
          content: entry.content,
        });
      }
    }

    results.sort((a, b) => b.score - a.score);
    return results.slice(0, Math.max(1, Math.min(25, top_k)));
  }

  async read_file(relative_path, { start_line = 1, line_count = 160 } = {}) {
    await this.ensure();

    const normalized = String(relative_path ?? "").replaceAll("\\", "/").replace(/^\/+/, "");
    const absolute = path.resolve(this.project_root, normalized);
    const relative = path.relative(this.project_root, absolute).replaceAll("\\", "/");
    if (relative.startsWith("..") || path.isAbsolute(relative) || !this.is_allowed_source_file(relative)) {
      throw new Error("source path is outside the indexed Spartan source roots");
    }

    const text = await fs.readFile(absolute, "utf8");
    const lines = text.split(/\r?\n/g);
    const safe_start = Math.max(1, Number.parseInt(String(start_line), 10) || 1);
    const safe_count = Math.max(1, Math.min(400, Number.parseInt(String(line_count), 10) || 160));
    const start_index = Math.min(lines.length, safe_start - 1);
    const end_index = Math.min(lines.length, start_index + safe_count);

    return {
      path: relative,
      start_line: start_index + 1,
      end_line: end_index,
      total_lines: lines.length,
      text: lines.slice(start_index, end_index).join("\n"),
    };
  }
}
