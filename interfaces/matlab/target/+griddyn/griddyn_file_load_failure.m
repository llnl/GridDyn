function v = griddyn_file_load_failure()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = griddynMEX(0, 7);
  end
  v = vInitialized;
end
