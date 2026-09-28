% check_stress.m — FR-TOOL-1 / XT-OCTAVE-1: an independent recomputation of a
% wafer's stress from its exported samples.csv, done in a different language
% with a different quadratic-fit implementation than the C++ machine, to
% cross-check the physics and the arithmetic, not just re-read the machine's
% own answer.
%
%   octave scripts/check_stress.m results/<run_id>/<wafer_id>/samples.csv
%
% What it does, mirroring PRD §5.1's inverse model and src/analysis/combine.cpp:
%   1. Reads samples.csv, keeps only flag==0 rows (already edge-excluded and
%      outlier-rejected by the machine — this script re-does the curve fit and
%      the Stoney equation, not the filtering; see the note above for why that
%      is still a real, independent check).
%   2. Fits z = a*s^2 + b*s + c per line with polyfit (least squares, exactly
%      as src/analysis/line_fit.cpp does, but Octave's own implementation).
%   3. curvature_per_m = 2*a for each line (s in mm, z in um: a's units of
%      um/mm^2 equal 1/m directly, since 1e-6 / (1e-3)^2 = 1, so no unit
%      conversion is needed — see the comment in combine.cpp / PRD §5.1).
%   4. Mean curvature over the lines (plain mean, matching combine_lines()).
%   5. Stoney: sigma = M_s * t_s^2 * (k_mean - k0) / (6 * t_f).
%   6. If summary.json sits next to samples.csv, reads the machine's own
%      stress_mpa from it and reports the percentage difference (target in
%      PRD §7.14 / day-6-spec.md XT-OCTAVE-1: within 0.5%).
%
% Wafer physical constants are not in samples.csv (a units/config decision,
% CLAUDE.md §5.1: constants live in config, not per-sample data), so this
% script takes them as arguments after the CSV path, defaulting to
% config/default.json's wafer block. Run with different values if your wafer
% used a different config.
%
%   octave scripts/check_stress.m CSV_PATH [thickness_um] [film_thickness_um] \
%       [biaxial_modulus_gpa] [initial_curvature_1_per_m]
%
% This is a script file, not a function file: the executable code runs first
% (so `octave check_stress.m ARGS` works from the shell) and the helper
% functions it calls are defined below it, which both Octave and MATLAB allow
% in a script as long as the code, not a `function` line, comes first.

args = argv();
if numel(args) < 1
  error('usage: octave check_stress.m CSV_PATH [thickness_um] [film_thickness_um] [biaxial_modulus_gpa] [k0]');
endif
csv_path = args{1};
thickness_um = pick_arg(args, 2, 775.0);
film_thickness_um = pick_arg(args, 3, 1.0);
biaxial_modulus_gpa = pick_arg(args, 4, 180.5);
k0 = pick_arg(args, 5, 0.002);

[line_index, s_mm, z_um, flag] = read_samples(csv_path);

kept = flag == 0;
lines = unique(line_index(kept));
if isempty(lines)
  error('check_stress: no flag==0 (kept) samples in %s', csv_path);
endif

curvatures = zeros(numel(lines), 1);
for i = 1:numel(lines)
  m = kept & (line_index == lines(i));
  s = s_mm(m);
  z = z_um(m);
  if numel(s) < 3
    error('check_stress: line %d has fewer than 3 kept samples', lines(i));
  endif
  p = polyfit(s, z, 2);  % p(1)*s^2 + p(2)*s + p(3)
  curvatures(i) = 2.0 * p(1);  % um/mm^2 == 1/m, see the header note
  printf('  line %d: %d kept samples, curvature = %.6f 1/m\n', lines(i), numel(s), curvatures(i));
endfor

k_mean = mean(curvatures);
m_s_pa = biaxial_modulus_gpa * 1e9;
t_s_m = thickness_um * 1e-6;
t_f_m = film_thickness_um * 1e-6;
stress_pa = m_s_pa * t_s_m^2 * (k_mean - k0) / (6.0 * t_f_m);
stress_mpa = stress_pa * 1e-6;

printf('\nmean curvature over %d lines: %.6f 1/m\n', numel(lines), k_mean);
printf('recomputed stress (Octave, independent fit): %.4f MPa\n', stress_mpa);

summary_path = fullfile(fileparts(csv_path), 'summary.json');
if exist(summary_path, 'file')
  machine_stress_mpa = read_machine_stress(summary_path);
  if isnan(machine_stress_mpa)
    printf('summary.json found but result.stress_mpa could not be parsed from it.\n');
  else
    diff_pct = 100.0 * abs(stress_mpa - machine_stress_mpa) / abs(machine_stress_mpa);
    printf('machine''s own stress (summary.json):        %.4f MPa\n', machine_stress_mpa);
    printf('difference: %.4f%%  (target: within 0.5%%)\n', diff_pct);
    if diff_pct <= 0.5
      printf('XT-OCTAVE-1: PASS\n');
    else
      printf('XT-OCTAVE-1: FAIL (exceeds 0.5%%)\n');
    endif
  endif
else
  printf('no summary.json next to %s; only the Octave-side number is reported.\n', csv_path);
endif

function v = pick_arg(args, idx, default_value)
  if numel(args) >= idx && ~isempty(args{idx})
    v = str2double(args{idx});
  else
    v = default_value;
  endif
endfunction

% Plain-text CSV reader (no toolbox/package dependency: readtable needs the
% Octave "io" forge package, which is not guaranteed installed). Columns:
% wafer_id,line_index,s_mm,z_um_raw,z_um_clean,flag
function [line_index, s_mm, z_um, flag] = read_samples(csv_path)
  fid = fopen(csv_path, 'r');
  if fid < 0
    error('check_stress: cannot open %s', csv_path);
  endif
  fgetl(fid);  % header, discarded
  line_index = [];
  s_mm = [];
  z_um = [];
  flag = [];
  line = fgetl(fid);
  while ischar(line)
    if ~isempty(strtrim(line))
      parts = strsplit(line, ',');
      line_index(end + 1, 1) = str2double(parts{2});
      s_mm(end + 1, 1) = str2double(parts{3});
      z_um(end + 1, 1) = str2double(parts{4});
      flag(end + 1, 1) = str2double(parts{6});
    endif
    line = fgetl(fid);
  endwhile
  fclose(fid);
endfunction

% Pulls result.stress_mpa out of summary.json without a JSON package: the file
% is small and the layout is fixed (src/analysis/writers/json_writer.cpp), so a
% direct regex match is simpler and more portable than a parser dependency.
function stress_mpa = read_machine_stress(summary_path)
  text = fileread(summary_path);
  tok = regexp(text, '"stress_mpa"\s*:\s*(-?[0-9.eE+-]+)', 'tokens');
  if isempty(tok)
    stress_mpa = NaN;
  else
    stress_mpa = str2double(tok{1}{1});
  endif
endfunction
